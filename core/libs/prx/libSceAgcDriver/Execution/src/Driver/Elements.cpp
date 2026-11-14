#include "prx/libSceAgcDriver/Execution/include/Driver/Elements.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Diagnostics.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Dispatch/DispatchTiming.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawTiming.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/EngineMarks.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DebugLabels.hpp"
#include "prx/libSceAgcDriver/Graphics/include/RuntimeSwitches.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libSceAgcDriver/Execution/include/QueueState.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace AgcDriver::Elements {

using namespace AgcDriver::DriverDetail;

namespace {

enum Type : std::uint8_t { Other = 0, Draw = 1, Dispatch = 2, Wait = 3 };

struct Entry {
    std::atomic<std::uint64_t> key{0};
    std::atomic<std::uint64_t> ns{0};
    std::atomic<std::uint64_t> packets{0};
    std::atomic<std::uint64_t> draws{0};
    std::atomic<std::uint64_t> dispatches{0};
    std::atomic<std::uint64_t> waitNs{0};
    std::atomic<std::uint64_t> commitNs{0};
    std::atomic<std::uint64_t> commits{0};
    std::atomic<std::uint64_t> prepareNs{0};
    std::atomic<std::uint64_t> skipped{0};
    // labels: queue id; materials/programs/engine: the label where the entry was first seen.
    std::atomic<std::uint64_t> aux{0};
    // programs: first and last program address.
    std::atomic<std::uint64_t> first{0};
    std::atomic<std::uint64_t> last{0};
    // labels: generation of the skip decision below.
    std::atomic<std::uint64_t> skipGeneration{0};
    std::atomic<std::uint32_t> skipMask{0};
    // labels: the dword offsets inside the submission where queue 0 entered the label (frame order).
    std::atomic<std::uint64_t> orderSum{0};
    std::atomic<std::uint64_t> orderCount{0};
};

constexpr std::uint64_t Mix(std::uint64_t value) {
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33u;
    return value;
}

template<std::size_t N>
struct Table {
    std::array<Entry, N> entries{};
    std::atomic<std::uint64_t> overflow{0};
    // Returns the slot of `key` (inserting it), or N when the table is full.
    std::uint32_t Slot(std::uint64_t key) {
        if (key == 0) key = 1;
        std::size_t index = Mix(key) & (N - 1);
        for (std::size_t probe = 0; probe < 64; ++probe, index = (index + 1) & (N - 1)) {
            auto& entry = entries[index];
            auto seen = entry.key.load(std::memory_order_acquire);
            if (seen == key) return static_cast<std::uint32_t>(index);
            if (seen == 0) {
                std::uint64_t expected = 0;
                if (entry.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) return static_cast<std::uint32_t>(index);
                if (expected == key) return static_cast<std::uint32_t>(index);
            }
        }
        overflow.fetch_add(1, std::memory_order_relaxed);
        return static_cast<std::uint32_t>(N);
    }
    Entry* At(std::uint32_t slot) { return slot < N ? &entries[slot] : nullptr; }
};

constexpr std::size_t LabelSlots = 8192;
constexpr std::size_t EngineSlots = 1024;
constexpr std::size_t MaterialSlots = 32768;
constexpr std::size_t ProgramSlots = 16384;

Table<LabelSlots>& Labels() { static auto* table = new Table<LabelSlots>(); return *table; }
Table<EngineSlots>& Engines() { static auto* table = new Table<EngineSlots>(); return *table; }
Table<MaterialSlots>& Materials() { static auto* table = new Table<MaterialSlots>(); return *table; }
Table<ProgramSlots>& Programs() { static auto* table = new Table<ProgramSlots>(); return *table; }

// Worker phases (APS5_PROFILE_DRAW) per label and per program: dispatch phases at [0, DriverPhaseCount), draw
// phases after them.
constexpr std::size_t CommitPhaseCount = 13;
constexpr const char* CommitPhaseNames[CommitPhaseCount] = {"commit: validate", "commit: vertex", "commit: setup", "commit: readTarget", "commit: prepare", "commit: lookup", "commit: resources", "commit: pipeline", "commit: record", "commit: keep", "commit: sync", "commit: writeBack", "commit: describe"};
constexpr std::size_t PhaseColumns = DriverPhaseCount + DrawDriverPhaseCount + CommitPhaseCount;
using PhaseRow = std::array<std::atomic<std::uint64_t>, PhaseColumns>;
std::vector<PhaseRow>& LabelPhases() { static auto* rows = new std::vector<PhaseRow>(LabelSlots); return *rows; }
std::vector<PhaseRow>& ProgramPhases() { static auto* rows = new std::vector<PhaseRow>(ProgramSlots); return *rows; }

const char* PhaseName(std::size_t column) {
    if (column < DriverPhaseCount) return DriverPhaseNames[column];
    if (column < DriverPhaseCount + DrawDriverPhaseCount) return DrawDriverPhaseNames[column - DriverPhaseCount];
    return CommitPhaseNames[column - DriverPhaseCount - DrawDriverPhaseCount];
}

thread_local const Tag* committing = nullptr;

// Label strings: the key of a label entry is a 64-bit hash of its text; the text is kept here for the report.
std::mutex textMutex;
std::unordered_map<std::uint64_t, std::string>& Texts() { static auto* texts = new std::unordered_map<std::uint64_t, std::string>(); return *texts; }

std::uint64_t HashText(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const char c : text) hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return hash == 0 ? 1 : hash;
}

std::string LabelText(std::uint32_t slot) {
    auto* entry = Labels().At(slot);
    if (entry == nullptr) return "(overflow)";
    std::lock_guard lock(textMutex);
    const auto found = Texts().find(entry->key.load(std::memory_order_relaxed));
    return found != Texts().end() ? found->second : "?";
}

struct Rules {
    std::uint64_t generation = 0;
    std::vector<std::string> passes, passDraws, passDispatches, passExact;
    // Pass rules listed after an "every <n>" line: the pass runs only on every n-th frame (presents).
    std::vector<std::string> periodicPasses, periodicExact;
    std::uint64_t period = 0;
    std::vector<std::uint64_t> programs, vas, materials, gvas, gmaterials;
    std::vector<std::uint32_t> kinds, gkinds, queues;
    bool any = false;
    bool guest = false;
    int lockPriority = -1;
    std::size_t pipeDepth = 0;
    std::size_t prepareAhead = 0;
    // commitdelay <us> / q0delay <us>: causal-profiling delays (busy waits) added to every draw-pipeline commit and
    // to every queue-0 draw or dispatch packet while listed.
    std::uint32_t commitDelayUs = 0;
    std::uint32_t q0DelayUs = 0;
    // gpoke <va> <u32>: a 4-byte value written into the game's data at an eboot VA while the rule is listed.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> pokes;
    // switch <APS5_NAME> on|off: a runtime switch of the in-run A/B registry set live while listed.
    std::vector<std::pair<std::string, bool>> switches;
};

// The causal-profiling delays of the current rules (commitdelay / q0delay), in microseconds.
std::atomic<std::uint32_t> commitDelay{0};
std::atomic<std::uint32_t> q0Delay{0};

void SpinMicroseconds(std::uint32_t us) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    while (std::chrono::steady_clock::now() < until) {
    }
}

// The environment values of switches a rule changed, restored when their rule goes away.
std::vector<std::pair<std::string, bool>> switchOriginals;
std::mutex switchMutex;

void ApplySwitches(const Rules& next) {
    std::lock_guard lock(switchMutex);
    for (auto it = switchOriginals.begin(); it != switchOriginals.end();) {
        const bool kept = std::any_of(next.switches.begin(), next.switches.end(), [&](const auto& item) { return item.first == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        if (const auto id = Graphics::RuntimeSwitches::Find(it->first)) Graphics::RuntimeSwitches::Assign(*id, it->second);
        std::fprintf(stderr, "[elem] switch %s restored to %s\n", it->first.c_str(), it->second ? "on" : "off");
        it = switchOriginals.erase(it);
    }
    for (const auto& [name, value] : next.switches) {
        const auto id = Graphics::RuntimeSwitches::Find(name);
        if (!id) {
            std::fprintf(stderr, "[elem] switch %s is not a runtime switch\n", name.c_str());
            continue;
        }
        if (std::none_of(switchOriginals.begin(), switchOriginals.end(), [&](const auto& original) { return original.first == name; })) switchOriginals.emplace_back(name, Graphics::RuntimeSwitches::Set(*id));
        Graphics::RuntimeSwitches::Assign(*id, value);
        std::fprintf(stderr, "[elem] switch %s %s\n", name.c_str(), value ? "on" : "off");
    }
}

// The original values of poked dwords, restored when their rule goes away.
std::mutex pokeMutex;
std::vector<std::pair<std::uint64_t, std::uint32_t>> pokedOriginals;

void ApplyPokes(const Rules& next) {
    std::lock_guard lock(pokeMutex);
    for (auto it = pokedOriginals.begin(); it != pokedOriginals.end();) {
        const bool kept = std::any_of(next.pokes.begin(), next.pokes.end(), [&](const auto& poke) { return poke.first == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        if (const auto host = EngineMarks::GuestAddress(it->first); host != 0 && GuestMemory::Accessible(reinterpret_cast<const void*>(host), 4, true)) {
            std::memcpy(reinterpret_cast<void*>(host), &it->second, 4);
            std::fprintf(stderr, "[elem] gpoke 0x%llx restored to 0x%x\n", static_cast<unsigned long long>(it->first), it->second);
        }
        it = pokedOriginals.erase(it);
    }
    for (const auto& [va, value] : next.pokes) {
        const auto host = EngineMarks::GuestAddress(va);
        if (host == 0 || !GuestMemory::Accessible(reinterpret_cast<const void*>(host), 4, true)) {
            std::fprintf(stderr, "[elem] gpoke 0x%llx refused: not writable\n", static_cast<unsigned long long>(va));
            continue;
        }
        if (std::none_of(pokedOriginals.begin(), pokedOriginals.end(), [&](const auto& original) { return original.first == va; })) {
            std::uint32_t original = 0;
            std::memcpy(&original, reinterpret_cast<const void*>(host), 4);
            pokedOriginals.emplace_back(va, original);
            std::fprintf(stderr, "[elem] gpoke 0x%llx 0x%x -> 0x%x\n", static_cast<unsigned long long>(va), original, value);
        }
        std::memcpy(reinterpret_cast<void*>(host), &value, 4);
    }
}

std::atomic<const Rules*> rules{nullptr};

std::uint32_t KindNumber(const std::string& name) {
    if (name == "item") return 1;
    if (name == "instanced") return 2;
    if (name == "instanced-b") return 3;
    if (name == "other") return 10;
    return 0;
}

void Reload(const char* path) {
    static std::mutex reload;
    static std::filesystem::file_time_type seen{};
    static std::uint64_t generation = 0;
    const std::unique_lock lock(reload, std::try_to_lock);
    if (!lock.owns_lock()) return;
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path, error);
    if (error || stamp == seen) return;
    seen = stamp;
    auto* next = new Rules();
    next->generation = ++generation;
    std::ifstream file(path);
    std::string line;
    std::size_t count = 0;
    while (std::getline(file, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        std::istringstream words(line);
        std::string verb;
        if (!(words >> verb)) continue;
        std::string rest;
        std::getline(words, rest);
        rest.erase(0, rest.find_first_not_of(" \t"));
        if (rest.empty()) continue;
        const auto hex = std::strtoull(rest.c_str(), nullptr, 16);
        ++count;
        if (verb == "every") {
            next->period = std::strtoull(rest.c_str(), nullptr, 10);
            --count;
        }
        else if (verb == "pass" && next->period > 1) next->periodicPasses.push_back(rest);
        else if (verb == "passx" && next->period > 1) next->periodicExact.push_back(rest);
        else if (verb == "pass") next->passes.push_back(rest);
        else if (verb == "passd") next->passDraws.push_back(rest);
        else if (verb == "passc") next->passDispatches.push_back(rest);
        else if (verb == "passx") next->passExact.push_back(rest);
        else if (verb == "prog") next->programs.push_back(hex);
        else if (verb == "kind") next->kinds.push_back(KindNumber(rest));
        else if (verb == "va") next->vas.push_back(hex);
        else if (verb == "mat") next->materials.push_back(hex);
        else if (verb == "queue") next->queues.push_back(static_cast<std::uint32_t>(hex));
        else if (verb == "gkind") next->gkinds.push_back(KindNumber(rest));
        else if (verb == "gva") next->gvas.push_back(hex);
        else if (verb == "gmat") next->gmaterials.push_back(hex);
        else if (verb == "lockprio") next->lockPriority = rest == "on" ? 1 : rest == "off" ? 0 : -1;
        else if (verb == "deferstores") next->switches.emplace_back("APS5_NO_DEFER_STORES", rest != "on");
        else if (verb == "pipedepth") next->pipeDepth = static_cast<std::size_t>(std::strtoull(rest.c_str(), nullptr, 10));
        else if (verb == "prepahead") next->prepareAhead = static_cast<std::size_t>(std::strtoull(rest.c_str(), nullptr, 10));
        else if (verb == "commitdelay") next->commitDelayUs = static_cast<std::uint32_t>(std::strtoul(rest.c_str(), nullptr, 10));
        else if (verb == "q0delay") next->q0DelayUs = static_cast<std::uint32_t>(std::strtoul(rest.c_str(), nullptr, 10));
        else if (verb == "switch") {
            std::istringstream values(rest);
            std::string name, value;
            if (values >> name >> value) next->switches.emplace_back(name, value == "on");
        }
        else if (verb == "gpoke") {
            std::istringstream values(rest);
            std::string va, value;
            if (values >> va >> value) next->pokes.emplace_back(std::strtoull(va.c_str(), nullptr, 16), static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 16)));
        }
        else --count;
    }
    next->any = count != 0;
    next->guest = !next->gkinds.empty() || !next->gvas.empty() || !next->gmaterials.empty();
    rules.store(next, std::memory_order_release);  // the previous rules are leaked: readers may still hold them
    GuestMemory::SetGpuLockPriority(next->lockPriority);
    DriverDetail::DrawPipeline::OverrideDepth(next->pipeDepth, next->prepareAhead);
    ApplyPokes(*next);
    ApplySwitches(*next);
    commitDelay.store(next->commitDelayUs, std::memory_order_relaxed);
    q0Delay.store(next->q0DelayUs, std::memory_order_relaxed);
    std::fprintf(stderr, "[elem] rules %llu: %zu lines from %s\n", static_cast<unsigned long long>(next->generation), count, path);
    std::fflush(stderr);
}

const Rules* CurrentRules() {
    static const char* const path = std::getenv("APS5_ELEM_FILE");
    if (path == nullptr) return nullptr;
    static std::atomic<std::int64_t> nextCheck{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto due = nextCheck.load(std::memory_order_relaxed);
    if (now >= due && nextCheck.compare_exchange_strong(due, now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(500)).count(), std::memory_order_relaxed)) Reload(path);
    return rules.load(std::memory_order_acquire);
}

template<typename T>
bool Contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

struct WorkerCache {
    const QueueState* queue = nullptr;
    std::uint64_t generation = ~std::uint64_t{0};
    std::uint32_t queueId = ~0u;
    std::uint32_t label = static_cast<std::uint32_t>(LabelSlots);
};

std::atomic<std::uint64_t> presents{0};

thread_local WorkerCache cache;
thread_local std::uint32_t lastLabel = ~0u;
thread_local Tag current;
thread_local std::uint64_t currentEngineKey = 0;
thread_local std::uint64_t currentMaterial = 0;
thread_local std::uint64_t drawFirst = 0;
thread_local std::uint64_t drawLast = 0;

std::uint32_t LabelFor(std::uint32_t queueId, const QueueState& queue) {
    if (cache.queue == &queue && cache.generation == queue.markerGeneration && cache.queueId == queueId) return cache.label;
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "q%x|", queueId);
    std::string text = prefix;
    for (std::size_t index = 0; index < queue.markers.size(); ++index) {
        if (index != 0) text += '/';
        text += queue.markers[index];
        if (text.size() > 200) break;
    }
    const auto key = HashText(text);
    const auto slot = Labels().Slot(key);
    if (auto* entry = Labels().At(slot); entry != nullptr && entry->aux.load(std::memory_order_relaxed) == 0) {
        entry->aux.store(static_cast<std::uint64_t>(queueId) + 1u, std::memory_order_relaxed);
        std::lock_guard lock(textMutex);
        Texts().emplace(key, text);
    }
    cache = {&queue, queue.markerGeneration, queueId, slot};
    return slot;
}

bool LabelMatches(const std::vector<std::string>& texts, const std::string& label) {
    for (const auto& text : texts) {
        if (label.find(text) != std::string::npos) return true;
    }
    return false;
}

// Bit 0: skip draws, bit 1: skip dispatches; cached per label and rules generation.
std::uint32_t LabelSkipMask(std::uint32_t slot, const Rules& active) {
    auto* entry = Labels().At(slot);
    if (entry == nullptr) return 0;
    if (entry->skipGeneration.load(std::memory_order_acquire) == active.generation) return entry->skipMask.load(std::memory_order_relaxed);
    const auto text = LabelText(slot);
    std::uint32_t mask = 0;
    if (LabelMatches(active.passes, text)) mask |= 3u;
    if (LabelMatches(active.passDraws, text)) mask |= 1u;
    if (LabelMatches(active.passDispatches, text)) mask |= 2u;
    if (Contains(active.passExact, text)) mask |= 3u;
    if (LabelMatches(active.periodicPasses, text) || Contains(active.periodicExact, text)) mask |= 12u;
    entry->skipMask.store(mask, std::memory_order_relaxed);
    entry->skipGeneration.store(active.generation, std::memory_order_release);
    return mask;
}

std::uint64_t DispatchProgram(const QueueState& queue) {
    return (static_cast<std::uint64_t>(DriverDetail::readRegister(queue.shader, 0x20c)) << 8u) | (static_cast<std::uint64_t>(DriverDetail::readRegister(queue.shader, 0x20d) & 0xffu) << 40u);
}

std::uint32_t ProgramSlot(char type, std::uint64_t first, std::uint64_t last) {
    const auto key = Mix(first ^ Mix(last + static_cast<std::uint64_t>(type)));
    const auto slot = Programs().Slot(key);
    if (auto* entry = Programs().At(slot); entry != nullptr && entry->first.load(std::memory_order_relaxed) == 0) {
        entry->first.store(first | (static_cast<std::uint64_t>(type) << 56u), std::memory_order_relaxed);
        entry->last.store(last, std::memory_order_relaxed);
        entry->aux.store(current.label, std::memory_order_relaxed);
    }
    return slot;
}

void Report();

const bool enabled = [] {
    const bool on = std::getenv("APS5_ELEM") != nullptr || std::getenv("APS5_ELEM_FILE") != nullptr;
    if (on) std::thread(Report).detach();
    // APS5_DEBUG_LABELS: the label of the packet being recorded (the committer's item, else the worker's packet)
    // names a VK_EXT_debug_utils range (DebugLabels.hpp).
    if (on && Graphics::DebugLabels::Enabled()) {
        Graphics::DebugLabels::SetProvider({
            +[]() -> std::uint32_t { return committing != nullptr ? committing->label : current.label; },
            +[](std::uint32_t slot) -> std::string { return LabelText(slot); },
        });
    }
    return on;
}();

}

bool Enabled() {
    return enabled;
}

void NoteFlip() {
    presents.fetch_add(1, std::memory_order_relaxed);
}

Tag CurrentTag() {
    return enabled ? current : Tag{};
}

bool PacketScope::Begin(std::uint32_t queueId, const QueueState& queue, std::uint32_t opcode, bool draw, std::size_t offset) {
    active = true;
    start = std::chrono::steady_clock::now();
    type = draw ? Draw : (opcode == 0x15 || opcode == 0x16) ? Dispatch : (opcode == 0x3c || opcode == 0x93) ? Wait : Other;
    current = {};
    current.label = LabelFor(queueId, queue);
    if (current.label != lastLabel) {
        lastLabel = current.label;
        if (auto* entry = Labels().At(current.label); entry != nullptr && queueId == 0) {
            entry->orderSum.fetch_add(offset, std::memory_order_relaxed);
            entry->orderCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    current.engineSlot = static_cast<std::uint32_t>(EngineSlots);
    current.materialSlot = static_cast<std::uint32_t>(MaterialSlots);
    current.programSlot = static_cast<std::uint32_t>(ProgramSlots);
    currentEngineKey = 0;
    currentMaterial = 0;
    drawFirst = drawLast = 0;
    if (type == Draw && EngineMarks::Enabled()) {
        const auto open = EngineMarks::CurrentKind();
        if (open.kind != 0) {
            currentEngineKey = (static_cast<std::uint64_t>(open.kind) << 56u) | open.va;
            current.engineSlot = Engines().Slot(currentEngineKey);
            if (auto* entry = Engines().At(current.engineSlot); entry != nullptr && entry->aux.load(std::memory_order_relaxed) == 0) entry->aux.store(current.label + 1u, std::memory_order_relaxed);
        }
        currentMaterial = EngineMarks::CurrentMaterial();
        if (currentMaterial != 0) {
            current.materialSlot = Materials().Slot(currentMaterial);
            if (auto* entry = Materials().At(current.materialSlot); entry != nullptr && entry->aux.load(std::memory_order_relaxed) == 0) entry->aux.store(current.label + 1u, std::memory_order_relaxed);
        }
    }
    std::uint64_t program = 0;
    if (type == Dispatch) {
        program = DispatchProgram(queue);
        current.programSlot = ProgramSlot('c', program, program);
    }
    if (type != Draw && type != Dispatch) return false;
    if (queueId == 0) {
        if (const auto us = q0Delay.load(std::memory_order_relaxed); us != 0) SpinMicroseconds(us);
    }
    const auto* active = CurrentRules();
    if (active == nullptr || !active->any) return false;
    bool skip = Contains(active->queues, queueId);
    if (!skip) {
        const auto mask = LabelSkipMask(current.label, *active);
        skip = (mask & (type == Draw ? 1u : 2u)) != 0;
        if (!skip && active->period > 1 && (mask & (type == Draw ? 4u : 8u)) != 0) skip = presents.load(std::memory_order_relaxed) % active->period != 0;
    }
    if (!skip && type == Dispatch) skip = Contains(active->programs, program);
    if (!skip && type == Draw && currentEngineKey != 0) {
        skip = Contains(active->kinds, static_cast<std::uint32_t>(currentEngineKey >> 56u)) || Contains(active->vas, currentEngineKey & ((std::uint64_t{1} << 56u) - 1u));
    }
    if (!skip && type == Draw && currentMaterial != 0) skip = Contains(active->materials, currentMaterial);
    skipped = skip;
    return skip;
}

PacketScope::~PacketScope() {
    if (!active) return;
    const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    const auto add = [&](Entry* entry) {
        if (entry == nullptr) return;
        entry->ns.fetch_add(ns, std::memory_order_relaxed);
        entry->packets.fetch_add(1, std::memory_order_relaxed);
        if (skipped) entry->skipped.fetch_add(1, std::memory_order_relaxed);
        else if (type == Draw) entry->draws.fetch_add(1, std::memory_order_relaxed);
        else if (type == Dispatch) entry->dispatches.fetch_add(1, std::memory_order_relaxed);
        if (type == Wait) entry->waitNs.fetch_add(ns, std::memory_order_relaxed);
    };
    add(Labels().At(current.label));
    if (type == Draw) {
        add(Engines().At(current.engineSlot));
        add(Materials().At(current.materialSlot));
        if (drawFirst != 0) add(Programs().At(current.programSlot));
    } else if (type == Dispatch) {
        add(Programs().At(current.programSlot));
    }
}

void NoteDrawPrograms(std::uint64_t first, std::uint64_t last) {
    if (!enabled) return;
    drawFirst = first;
    drawLast = last;
    current.programSlot = ProgramSlot('d', first, last);
}

void NotePhases(bool dispatch, const double* ms, std::size_t count) {
    if (!enabled || ms == nullptr) return;
    const std::size_t base = dispatch ? 0 : DriverPhaseCount;
    const std::size_t limit = dispatch ? DriverPhaseCount : DrawDriverPhaseCount;
    PhaseRow* label = current.label < LabelSlots ? &LabelPhases()[current.label] : nullptr;
    PhaseRow* program = current.programSlot < ProgramSlots ? &ProgramPhases()[current.programSlot] : nullptr;
    for (std::size_t index = 0; index < std::min(count, limit); ++index) {
        if (ms[index] <= 0) continue;
        const auto ns = static_cast<std::uint64_t>(ms[index] * 1e6);
        if (label != nullptr) (*label)[base + index].fetch_add(ns, std::memory_order_relaxed);
        if (program != nullptr) (*program)[base + index].fetch_add(ns, std::memory_order_relaxed);
    }
}

void SetCommitTag(const Tag* tag) {
    committing = tag;
}

void NoteCommitPhases(const double* us, std::size_t count) {
    if (!enabled || us == nullptr || committing == nullptr) return;
    const std::size_t base = DriverPhaseCount + DrawDriverPhaseCount;
    PhaseRow* label = committing->label < LabelSlots ? &LabelPhases()[committing->label] : nullptr;
    PhaseRow* program = committing->programSlot < ProgramSlots ? &ProgramPhases()[committing->programSlot] : nullptr;
    for (std::size_t index = 0; index < std::min(count, CommitPhaseCount); ++index) {
        if (us[index] <= 0) continue;
        const auto ns = static_cast<std::uint64_t>(us[index] * 1e3);
        if (label != nullptr) (*label)[base + index].fetch_add(ns, std::memory_order_relaxed);
        if (program != nullptr) (*program)[base + index].fetch_add(ns, std::memory_order_relaxed);
    }
}

bool SkipDrawPrograms(std::uint64_t first, std::uint64_t last) {
    if (!enabled) return false;
    const auto* active = CurrentRules();
    if (active == nullptr || active->programs.empty()) return false;
    if (!Contains(active->programs, first) && !Contains(active->programs, last)) return false;
    if (auto* entry = Programs().At(current.programSlot)) entry->skipped.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void DelayCommit() {
    if (const auto us = commitDelay.load(std::memory_order_relaxed); us != 0) SpinMicroseconds(us);
}

void NoteCommit(const Tag& tag, std::uint64_t ns, bool prepare) {
    if (!enabled) return;
    const auto add = [&](Entry* entry) {
        if (entry == nullptr) return;
        if (prepare) {
            entry->prepareNs.fetch_add(ns, std::memory_order_relaxed);
        } else {
            entry->commitNs.fetch_add(ns, std::memory_order_relaxed);
            entry->commits.fetch_add(1, std::memory_order_relaxed);
        }
    };
    add(Labels().At(tag.label));
    add(Engines().At(tag.engineSlot));
    add(Materials().At(tag.materialSlot));
    add(Programs().At(tag.programSlot));
}

bool GuestSkip(std::uint32_t kind, std::uint64_t va, std::uint64_t material) {
    if (!enabled) return false;
    const auto* active = CurrentRules();
    if (active == nullptr || !active->guest) return false;
    return Contains(active->gkinds, kind) || (va != 0 && Contains(active->gvas, va)) || (material != 0 && Contains(active->gmaterials, material));
}

namespace {

struct Snapshot {
    std::uint64_t ns = 0, packets = 0, draws = 0, dispatches = 0, waitNs = 0, commitNs = 0, commits = 0, prepareNs = 0, skipped = 0;
};

Snapshot Read(const Entry& entry) {
    return {entry.ns.load(std::memory_order_relaxed), entry.packets.load(std::memory_order_relaxed), entry.draws.load(std::memory_order_relaxed), entry.dispatches.load(std::memory_order_relaxed), entry.waitNs.load(std::memory_order_relaxed), entry.commitNs.load(std::memory_order_relaxed), entry.commits.load(std::memory_order_relaxed), entry.prepareNs.load(std::memory_order_relaxed), entry.skipped.load(std::memory_order_relaxed)};
}

Snapshot Delta(const Snapshot& now, const Snapshot& before) {
    return {now.ns - before.ns, now.packets - before.packets, now.draws - before.draws, now.dispatches - before.dispatches, now.waitNs - before.waitNs, now.commitNs - before.commitNs, now.commits - before.commits, now.prepareNs - before.prepareNs, now.skipped - before.skipped};
}

template<std::size_t N>
std::vector<std::pair<std::uint32_t, Snapshot>> Deltas(Table<N>& table, std::vector<Snapshot>& last) {
    if (last.size() != N) last.assign(N, {});
    std::vector<std::pair<std::uint32_t, Snapshot>> rows;
    for (std::size_t index = 0; index < N; ++index) {
        const auto& entry = table.entries[index];
        if (entry.key.load(std::memory_order_relaxed) == 0) continue;
        const auto now = Read(entry);
        const auto delta = Delta(now, last[index]);
        last[index] = now;
        if (delta.packets == 0 && delta.commits == 0 && delta.prepareNs == 0) continue;
        rows.emplace_back(static_cast<std::uint32_t>(index), delta);
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.ns + a.second.commitNs + a.second.prepareNs > b.second.ns + b.second.commitNs + b.second.prepareNs; });
    return rows;
}

double Ms(std::uint64_t ns) {
    return static_cast<double>(ns) / 1e6;
}

// "[elem-ph] <round> <L|P> <slot text> total=<ms> <phase>=<ms>;..." for the top `count` rows of `rows`: the phases
// of the round (deltas against `last`), largest first.
template<std::size_t N>
void PhaseLines(std::string& text, char kind, std::uint64_t round, const std::vector<std::pair<std::uint32_t, Snapshot>>& rows, std::vector<PhaseRow>& phases, std::vector<std::array<std::uint64_t, PhaseColumns>>& last, std::size_t count, const std::function<std::string(std::uint32_t)>& name) {
    if (last.size() != N) last.assign(N, {});
    for (std::size_t index = 0; index < std::min(count, rows.size()); ++index) {
        const auto slot = rows[index].first;
        if (slot >= N) continue;
        std::array<std::pair<std::uint64_t, std::size_t>, PhaseColumns> deltas{};
        std::uint64_t total = 0;
        for (std::size_t column = 0; column < PhaseColumns; ++column) {
            const auto now = phases[slot][column].load(std::memory_order_relaxed);
            deltas[column] = {now - last[slot][column], column};
            total += deltas[column].first;
            last[slot][column] = now;
        }
        if (total == 0) continue;
        std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        char line[160];
        std::snprintf(line, sizeof(line), "[elem-ph] %llu %c total=%.1f", static_cast<unsigned long long>(round), kind, static_cast<double>(total) / 1e6);
        text += line;
        for (std::size_t column = 0; column < 6 && deltas[column].first != 0; ++column) {
            std::snprintf(line, sizeof(line), " %s=%.1f;", PhaseName(deltas[column].second), static_cast<double>(deltas[column].first) / 1e6);
            text += line;
        }
        text += " " + name(slot) + "\n";
    }
}

void Report() {
    static const std::size_t top = [] { const char* text = std::getenv("APS5_ELEM_TOP"); return text != nullptr ? static_cast<std::size_t>(std::strtoul(text, nullptr, 10)) : std::size_t{80}; }();
    std::vector<Snapshot> lastLabels, lastEngines, lastMaterials, lastPrograms;
    std::vector<std::array<std::uint64_t, PhaseColumns>> lastLabelPhases, lastProgramPhases;
    std::uint64_t round = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        ++round;
        std::string text;
        char line[640];
        // Per label (pass): worker ms, commit ms, prepare ms, wait ms, draws, dispatches, packets, skipped, queue, label.
        const auto labels = Deltas(Labels(), lastLabels);
        std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> queues;
        for (const auto& [slot, delta] : labels) {
            auto& sums = queues[Labels().entries[slot].aux.load(std::memory_order_relaxed)];
            sums.first += delta.ns;
            sums.second += delta.commitNs;
        }
        // HTILE pre-syncs of depth draws in this round (VulkanDevice::PresyncDrawDepth): each waits for the GPU.
        static std::uint64_t lastPresyncs = 0, lastPresyncUs = 0;
        const auto presyncs = VulkanDevice::DepthPresyncCounts();
        const auto presyncCount = presyncs.waited - lastPresyncs;
        const auto presyncUs = presyncs.waitedUs - lastPresyncUs;
        lastPresyncs = presyncs.waited;
        lastPresyncUs = presyncs.waitedUs;
        // GPU-ordered storage write-backs (Texture.cpp GpuOrderedReasons) in this round, and the flushes their
        // conditions kept waiting ([elem-defer]).
        static Graphics::StorageTexture::GpuOrderCounts lastOrdering{};
        const auto ordering = Graphics::StorageTexture::GpuOrderTotals();
        std::snprintf(line, sizeof(line), "[elem] round %llu (10 s): %zu labels active; %llu deferred stores; %llu depth pre-syncs %.0f ms; per queue worker/commit ms:", static_cast<unsigned long long>(round), labels.size(), static_cast<unsigned long long>(ordering.deferred - lastOrdering.deferred), static_cast<unsigned long long>(presyncCount), presyncUs / 1000.0);
        text += line;
        for (const auto& [queue, sums] : queues) {
            std::snprintf(line, sizeof(line), " q%llx %.0f/%.0f;", static_cast<unsigned long long>(queue == 0 ? 0 : queue - 1), Ms(sums.first), Ms(sums.second));
            text += line;
        }
        text += "\n";
        std::snprintf(line, sizeof(line), "[elem-defer] %llu kept waiting: unwatched %llu, completions %llu, lane %llu; lease waits %llu (%llu stamped the stored blocks); blocks left to a pending writer %llu; leases skipped %llu, sampled %llu%s%s\n", static_cast<unsigned long long>(round), static_cast<unsigned long long>(ordering.unwatched - lastOrdering.unwatched), static_cast<unsigned long long>(ordering.completions - lastOrdering.completions), static_cast<unsigned long long>(ordering.lane - lastOrdering.lane), static_cast<unsigned long long>(ordering.leaseWaits - lastOrdering.leaseWaits), static_cast<unsigned long long>(ordering.leaseStamped - lastOrdering.leaseStamped), static_cast<unsigned long long>(ordering.keptForWriter - lastOrdering.keptForWriter), static_cast<unsigned long long>(ordering.leaseSkipped - lastOrdering.leaseSkipped), static_cast<unsigned long long>(ordering.leaseSampled - lastOrdering.leaseSampled), ordering.leaseGuardTripped ? ", lease guard tripped" : "", Graphics::RuntimeSwitches::Set(Graphics::RuntimeSwitch::NoDeferStores) ? " (APS5_NO_DEFER_STORES)" : "");
        text += line;
        lastOrdering = ordering;
        for (std::size_t index = 0; index < std::min(top, labels.size()); ++index) {
            const auto& [slot, d] = labels[index];
            const auto& labelEntry = Labels().entries[slot];
            const auto orders = labelEntry.orderCount.load(std::memory_order_relaxed);
            const auto order = orders != 0 ? labelEntry.orderSum.load(std::memory_order_relaxed) / orders : 0;
            std::snprintf(line, sizeof(line), "[elem-pass] %llu %.1f %.1f %.1f %.1f %llu %llu %llu %llu @%llu %s\n", static_cast<unsigned long long>(round), Ms(d.ns), Ms(d.commitNs), Ms(d.prepareNs), Ms(d.waitNs), static_cast<unsigned long long>(d.draws), static_cast<unsigned long long>(d.dispatches), static_cast<unsigned long long>(d.packets), static_cast<unsigned long long>(d.skipped), static_cast<unsigned long long>(order), LabelText(slot).c_str());
            text += line;
        }
        PhaseLines<LabelSlots>(text, 'L', round, labels, LabelPhases(), lastLabelPhases, 30, [](std::uint32_t slot) { return LabelText(slot); });
        const auto engines = Deltas(Engines(), lastEngines);
        for (std::size_t index = 0; index < std::min(top, engines.size()); ++index) {
            const auto& [slot, d] = engines[index];
            const auto key = Engines().entries[slot].key.load(std::memory_order_relaxed);
            std::snprintf(line, sizeof(line), "[elem-engine] %llu %.1f %.1f %.1f %llu %llu kind=%llu va=0x%llx\n", static_cast<unsigned long long>(round), Ms(d.ns), Ms(d.commitNs), Ms(d.prepareNs), static_cast<unsigned long long>(d.draws), static_cast<unsigned long long>(d.skipped), static_cast<unsigned long long>(key >> 56u), static_cast<unsigned long long>(key & ((std::uint64_t{1} << 56u) - 1u)));
            text += line;
        }
        const auto materials = Deltas(Materials(), lastMaterials);
        for (std::size_t index = 0; index < std::min(top, materials.size()); ++index) {
            const auto& [slot, d] = materials[index];
            const auto& entry = Materials().entries[slot];
            const auto seen = entry.aux.load(std::memory_order_relaxed);
            std::snprintf(line, sizeof(line), "[elem-mat] %llu %.1f %.1f %.1f %llu %llu 0x%llx %s\n", static_cast<unsigned long long>(round), Ms(d.ns), Ms(d.commitNs), Ms(d.prepareNs), static_cast<unsigned long long>(d.draws), static_cast<unsigned long long>(d.skipped), static_cast<unsigned long long>(entry.key.load(std::memory_order_relaxed)), seen != 0 ? LabelText(static_cast<std::uint32_t>(seen - 1)).c_str() : "-");
            text += line;
        }
        const auto programs = Deltas(Programs(), lastPrograms);
        for (std::size_t index = 0; index < std::min(top, programs.size()); ++index) {
            const auto& [slot, d] = programs[index];
            const auto& entry = Programs().entries[slot];
            const auto first = entry.first.load(std::memory_order_relaxed);
            std::snprintf(line, sizeof(line), "[elem-prog] %llu %.1f %.1f %.1f %llu %llu %c 0x%llx 0x%llx %s\n", static_cast<unsigned long long>(round), Ms(d.ns), Ms(d.commitNs), Ms(d.prepareNs), static_cast<unsigned long long>(d.draws + d.dispatches), static_cast<unsigned long long>(d.skipped), static_cast<char>(first >> 56u), static_cast<unsigned long long>(first & ((std::uint64_t{1} << 56u) - 1u)), static_cast<unsigned long long>(entry.last.load(std::memory_order_relaxed)), LabelText(static_cast<std::uint32_t>(entry.aux.load(std::memory_order_relaxed))).c_str());
            text += line;
        }
        PhaseLines<ProgramSlots>(text, 'P', round, programs, ProgramPhases(), lastProgramPhases, 30, [](std::uint32_t slot) {
            const auto& entry = Programs().entries[slot];
            const auto first = entry.first.load(std::memory_order_relaxed);
            char name[64];
            std::snprintf(name, sizeof(name), "%c 0x%llx 0x%llx", static_cast<char>(first >> 56u), static_cast<unsigned long long>(first & ((std::uint64_t{1} << 56u) - 1u)), static_cast<unsigned long long>(entry.last.load(std::memory_order_relaxed)));
            return std::string(name);
        });
        std::fputs(text.c_str(), stderr);
        std::fflush(stderr);
    }
}

}

}

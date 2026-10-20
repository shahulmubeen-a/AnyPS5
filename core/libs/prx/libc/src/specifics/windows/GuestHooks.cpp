#ifdef _WIN32
#include <windows.h>
#include "prx/libc/include/GuestHooks.hpp"
#include "prx/libc/include/general/AsmMacros.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" void GuestHookEnter_nid_no_patch();

namespace {

constexpr std::size_t JumpBytes = 5;
constexpr std::size_t SlotBytes = 128;
constexpr std::size_t BlockBytes = 1u << 20;
constexpr std::size_t MaxHandlers = 64;

struct HookState {
    std::string handler;
    std::string label;
    GuestHookInfo info{};
    std::atomic<GuestHookHandler> function{nullptr};
    std::atomic<std::uint64_t> hits{0};
    std::uint64_t reported = 0;
};

// Read by GuestHookEnter: the stub loads the record into r11 and the thunk ends with jmp *(%r11).
struct HookRecord {
    void* resume;
    HookState* state;
};
static_assert(offsetof(HookRecord, resume) == 0);
static_assert(offsetof(GuestHookContext, r11) == 256 && offsetof(GuestHookContext, rdi) == 256 + 64 && offsetof(GuestHookContext, returnAddress) == 256 + 72);

struct NamedHandler {
    std::atomic<const char*> name{nullptr};
    std::atomic<GuestHookHandler> handler{nullptr};
};

std::array<NamedHandler, MaxHandlers> g_handlers;
std::atomic<std::size_t> g_handlerCount{0};
std::mutex g_registerMutex;

std::mutex g_installMutex;
std::deque<HookState> g_states;
std::uint8_t* g_block = nullptr;
std::size_t g_blockUsed = 0;
bool g_reporterStarted = false;

std::mutex g_printMutex;

void Print(const char* text) {
    std::lock_guard lock(g_printMutex);
    std::fputs(text, stderr);
    std::fflush(stderr);
}

GuestHookHandler FindHandler(const char* name) {
    const auto count = g_handlerCount.load(std::memory_order_acquire);
    for (std::size_t index = 0; index < count; ++index) {
        const char* entry = g_handlers[index].name.load(std::memory_order_acquire);
        if (entry != nullptr && std::strcmp(entry, name) == 0) return g_handlers[index].handler.load(std::memory_order_acquire);
    }
    return nullptr;
}

std::uintptr_t g_imageBase = 0;
std::uintptr_t g_elf0 = 0;
std::size_t g_elf0Size = 0;
std::vector<GuestImageSection> g_sections;
std::vector<std::uintptr_t> g_sectionHosts;

bool GuestSectionName(const BYTE* name) {
    if (std::memcmp(name, ".elf", 4) != 0 || name[4] < '0' || name[4] > '9') return false;
    std::size_t index = 5;
    while (index < IMAGE_SIZEOF_SHORT_NAME && name[index] >= '0' && name[index] <= '9') ++index;
    while (index < IMAGE_SIZEOF_SHORT_NAME) {
        if (name[index++] != 0) return false;
    }
    return true;
}

void FindGuestImage() {
    if (g_elf0 != 0) return;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const auto* first = IMAGE_FIRST_SECTION(nt);
    const IMAGE_SECTION_HEADER* elf0 = nullptr;
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        if (std::memcmp(first[index].Name, ".elf0\0\0\0", 8) == 0) elf0 = &first[index];
    }
    if (elf0 == nullptr) return;
    g_imageBase = base;
    g_elf0 = base + elf0->VirtualAddress;
    g_elf0Size = elf0->Misc.VirtualSize;
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = first[index];
        if (!GuestSectionName(section.Name) || section.VirtualAddress < elf0->VirtualAddress) continue;
        GuestImageSection entry{section.VirtualAddress - elf0->VirtualAddress, section.Misc.VirtualSize, section.Characteristics, {}};
        std::memcpy(entry.name, section.Name, IMAGE_SIZEOF_SHORT_NAME);
        g_sections.push_back(entry);
        g_sectionHosts.push_back(base + section.VirtualAddress);
    }
}

bool Readable(DWORD protect) {
    const DWORD kind = protect & 0xff;
    return (protect & PAGE_GUARD) == 0 && (kind == PAGE_READONLY || kind == PAGE_READWRITE || kind == PAGE_WRITECOPY || kind == PAGE_EXECUTE_READ || kind == PAGE_EXECUTE_READWRITE || kind == PAGE_EXECUTE_WRITECOPY);
}

bool Executable(DWORD protect) {
    const DWORD kind = protect & 0xff;
    return kind == PAGE_EXECUTE || kind == PAGE_EXECUTE_READ || kind == PAGE_EXECUTE_READWRITE || kind == PAGE_EXECUTE_WRITECOPY;
}

std::string DescribeAddress(std::uint64_t address) {
    char text[64];
    if (g_elf0 != 0 && address >= g_elf0 && address < g_elf0 + g_elf0Size) {
        std::snprintf(text, sizeof(text), "VA 0x%llx", static_cast<unsigned long long>(address - g_elf0));
    } else {
        std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(address));
    }
    return text;
}

std::uint32_t LogLimit() {
    static const std::uint32_t limit = [] {
        const char* value = std::getenv("APS5_GUEST_HOOKS_LOG");
        if (value == nullptr || *value == '\0') return 16u;
        return static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
    }();
    return limit;
}

void PrintArguments(const GuestHookContext& context, const GuestHookInfo& info, std::uint64_t hit, bool withMemory) {
    if (hit > LogLimit()) return;
    float xmm0 = 0;
    std::memcpy(&xmm0, context.ymm[0], sizeof(xmm0));
    std::string line(512, '\0');
    const int n = std::snprintf(line.data(), line.size(), "[hook] %s #%llu thread %lu from %s: rdi=0x%llx rsi=0x%llx rdx=0x%llx rcx=0x%llx r8=0x%llx r9=0x%llx xmm0=%g\n",
                                info.label, static_cast<unsigned long long>(hit), static_cast<unsigned long>(GetCurrentThreadId()), DescribeAddress(context.returnAddress).c_str(),
                                static_cast<unsigned long long>(context.rdi), static_cast<unsigned long long>(context.rsi), static_cast<unsigned long long>(context.rdx),
                                static_cast<unsigned long long>(context.rcx), static_cast<unsigned long long>(context.r8), static_cast<unsigned long long>(context.r9), static_cast<double>(xmm0));
    line.resize(n > 0 ? std::min<std::size_t>(static_cast<std::size_t>(n), line.size() - 1) : 0);
    if (withMemory) {
        for (const auto [name, pointer] : {std::pair{"rdi", context.rdi}, std::pair{"rsi", context.rsi}}) {
            std::array<std::uint8_t, 64> bytes{};
            SIZE_T read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(pointer), bytes.data(), bytes.size(), &read) || read != bytes.size()) continue;
            line += "    [";
            line += name;
            line += "]";
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                char hex[4];
                std::snprintf(hex, sizeof(hex), index % 8 == 0 ? " %02x" : "%02x", bytes[index]);
                line += hex;
            }
            line += '\n';
        }
    }
    Print(line.c_str());
}

void CountHandler(GuestHookContext&, const GuestHookInfo&, std::uint64_t) {}

void ArgsHandler(GuestHookContext& context, const GuestHookInfo& info, std::uint64_t hit) {
    PrintArguments(context, info, hit, false);
}

void DumpHandler(GuestHookContext& context, const GuestHookInfo& info, std::uint64_t hit) {
    PrintArguments(context, info, hit, true);
}

void Reporter() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::string text;
        {
            std::lock_guard lock(g_installMutex);
            for (auto& state : g_states) {
                const auto hits = state.hits.load(std::memory_order_relaxed);
                if (hits == state.reported) continue;
                char part[160];
                std::snprintf(part, sizeof(part), " %s %llu (total %llu);", state.label.c_str(), static_cast<unsigned long long>(hits - state.reported), static_cast<unsigned long long>(hits));
                text += part;
                state.reported = hits;
            }
        }
        if (!text.empty()) Print(("[hooks] (10 s):" + text + "\n").c_str());
    }
}

std::uint8_t* AllocateNear(std::uintptr_t target) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const std::uintptr_t granularity = system.dwAllocationGranularity;
    const std::uintptr_t reach = 0x70000000;
    const std::uintptr_t start = (target & ~(granularity - 1));
    for (std::uintptr_t probe = start - granularity; probe > granularity && start - probe < reach; probe -= granularity) {
        if (void* block = VirtualAlloc(reinterpret_cast<void*>(probe), BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return static_cast<std::uint8_t*>(block);
    }
    for (std::uintptr_t probe = start + granularity; probe - start < reach; probe += granularity) {
        if (void* block = VirtualAlloc(reinterpret_cast<void*>(probe), BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return static_cast<std::uint8_t*>(block);
    }
    return nullptr;
}

bool Fits32(std::int64_t value) {
    return value >= INT32_MIN && value <= INT32_MAX;
}

void Explain(char* why, std::size_t whyBytes, const std::string& text) {
    if (why == nullptr || whyBytes == 0) return;
    const auto n = std::min(text.size(), whyBytes - 1);
    std::memcpy(why, text.data(), n);
    why[n] = '\0';
}

void Put64(std::uint8_t* at, std::uint64_t value) {
    std::memcpy(at, &value, sizeof(value));
}

std::string Hex(const std::uint8_t* bytes, std::size_t count) {
    std::string text;
    for (std::size_t index = 0; index < count; ++index) {
        char part[4];
        std::snprintf(part, sizeof(part), "%02x", bytes[index]);
        text += part;
    }
    return text;
}

bool ParseHex(const std::string& text, std::vector<std::uint8_t>& out) {
    if (text.size() % 2 != 0) return false;
    out.clear();
    for (std::size_t index = 0; index < text.size(); index += 2) {
        char* end = nullptr;
        const std::string part = text.substr(index, 2);
        const auto value = std::strtoul(part.c_str(), &end, 16);
        if (end == nullptr || *end != '\0') return false;
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return true;
}

void LoadTable() {
    const char* path = std::getenv("APS5_GUEST_HOOKS");
    if (path == nullptr || *path == '\0') return;
    FindGuestImage();
    if (g_elf0 == 0) {
        Print("[hooks] APS5_GUEST_HOOKS is set but app.exe has no .elf0 section; no hooks installed\n");
        return;
    }
    std::ifstream file(path);
    if (!file) {
        Print((std::string("[hooks] cannot open ") + path + "\n").c_str());
        return;
    }
    std::string line;
    std::size_t installed = 0;
    std::size_t patched = 0;
    std::size_t refused = 0;
    std::size_t number = 0;
    while (std::getline(file, line)) {
        ++number;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream words(line);
        std::string handler, vaText, bytesText, replacementText, label;
        if (!(words >> handler)) continue;
        const bool patch = handler == "patch";
        std::vector<std::uint8_t> expected;
        std::vector<std::uint8_t> replacement;
        std::string why;
        std::uint64_t va = 0;
        std::uintptr_t host = 0;
        const char* sectionName = "";
        if (!(words >> vaText >> bytesText) || (patch && !(words >> replacementText))) {
            why = patch ? "expected: patch <VA> <expected bytes> <replacement bytes> [label]" : "expected: <handler> <VA> <bytes> [label]";
        } else {
            char* end = nullptr;
            va = std::strtoull(vaText.c_str(), &end, 16);
            if (end == nullptr || *end != '\0') why = "VA is not hexadecimal";
            else if (!ParseHex(bytesText, expected) || expected.size() < (patch ? 1u : 8u)) why = patch ? "expected bytes must be hexadecimal" : "expected bytes must be at least 8 hexadecimal bytes";
            else if (patch && (!ParseHex(replacementText, replacement) || replacement.size() != expected.size())) why = "replacement bytes must be hexadecimal and as long as the expected bytes";
            else {
                char reason[192] = {};
                const int index = FindGuestTargetSection_nid_no_patch(g_sections.data(), g_sections.size(), va, expected.size(), !patch, reason, sizeof(reason));
                if (index < 0) {
                    why = reason;
                } else {
                    const auto& section = g_sections[static_cast<std::size_t>(index)];
                    host = g_sectionHosts[static_cast<std::size_t>(index)] + static_cast<std::uintptr_t>(va - section.va);
                    sectionName = section.name;
                }
            }
        }
        std::getline(words >> std::ws, label);
        if (label.empty()) label = handler + "@" + vaText;
        if (why.empty()) {
            char reason[256] = {};
            if (patch && ApplyGuestPatch_nid_no_patch(reinterpret_cast<void*>(host), expected.data(), replacement.data(), expected.size(), reason, sizeof(reason))) {
                ++patched;
                Print(("[hooks] patch " + label + " at VA 0x" + vaText + " (" + sectionName + "): " + Hex(expected.data(), expected.size()) + " -> " + Hex(replacement.data(), replacement.size()) + "\n").c_str());
                continue;
            }
            if (!patch && InstallGuestHook_nid_no_patch(reinterpret_cast<void*>(host), expected.data(), expected.size(), handler.c_str(), label.c_str(), va, reason, sizeof(reason))) {
                ++installed;
                continue;
            }
            why = reason;
        }
        ++refused;
        Print(("[hooks] line " + std::to_string(number) + " refused: " + why + "\n").c_str());
    }
    char summary[192];
    std::snprintf(summary, sizeof(summary), "[hooks] %zu installed, %zu patched, %zu refused from %s\n", installed, patched, refused, path);
    Print(summary);
}

const bool g_guestHooksLoaded = [] {
    RegisterGuestHookHandler_nid_no_patch("count", &CountHandler);
    RegisterGuestHookHandler_nid_no_patch("args", &ArgsHandler);
    RegisterGuestHookHandler_nid_no_patch("dump", &DumpHandler);
    LoadTable();
    return true;
}();

}

extern "C" {

bool DecodeGuestHookInstruction_nid_no_patch(const std::uint8_t* b, std::size_t available, GuestHookInstruction* out) {
    if (b == nullptr || out == nullptr || available == 0) return false;
    std::size_t j = 0;
    std::uint8_t rex = 0;
    if (b[0] >= 0x40 && b[0] <= 0x4f) {
        rex = b[0];
        j = 1;
    }
    if (j >= available) return false;
    const std::uint8_t opcode = b[j];
    const bool wide = (rex & 8) != 0;
    if (opcode >= 0x50 && opcode <= 0x5f && !wide) {
        *out = {j + 1, -1};
        return j + 1 <= available;
    }
    if (opcode >= 0xb8 && opcode <= 0xbf) {
        *out = {j + (wide ? 9u : 5u), -1};
        return out->length <= available;
    }
    std::size_t immediate = 0;
    switch (opcode) {
        case 0x01: case 0x03: case 0x09: case 0x0b: case 0x21: case 0x23: case 0x29: case 0x2b: case 0x31: case 0x33:
        case 0x39: case 0x3b: case 0x63: case 0x84: case 0x85: case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8d:
            break;
        case 0x80: case 0x83: case 0xc6:
            immediate = 1;
            break;
        case 0x81: case 0xc7:
            immediate = 4;
            break;
        default:
            return false;
    }
    if (j + 1 >= available) return false;
    const std::uint8_t modrm = b[j + 1];
    const unsigned mod = modrm >> 6;
    const unsigned rm = modrm & 7;
    if (mod == 3) {
        if (opcode == 0x8d) return false;
        *out = {j + 2 + immediate, -1};
        return out->length <= available;
    }
    if (mod == 0 && rm == 5) {
        *out = {j + 6 + immediate, static_cast<int>(j + 2)};
        return out->length <= available;
    }
    std::size_t length = j + 2;
    if (rm == 4) {
        if (j + 2 >= available) return false;
        if (mod == 0 && (b[j + 2] & 7) == 5) return false;
        ++length;
    }
    length += mod == 1 ? 1 : mod == 2 ? 4 : 0;
    *out = {length + immediate, -1};
    return out->length <= available;
}

bool RegisterGuestHookHandler_nid_no_patch(const char* name, GuestHookHandler handler) {
    if (name == nullptr || handler == nullptr) return false;
    std::lock_guard lock(g_registerMutex);
    if (FindHandler(name) != nullptr) return false;
    const auto index = g_handlerCount.load(std::memory_order_relaxed);
    if (index >= MaxHandlers) return false;
    g_handlers[index].handler.store(handler, std::memory_order_release);
    g_handlers[index].name.store(name, std::memory_order_release);
    g_handlerCount.store(index + 1, std::memory_order_release);
    return true;
}

bool InstallGuestHook_nid_no_patch(void* address, const std::uint8_t* expected, std::size_t expectedBytes, const char* handler, const char* label, std::uint64_t va, char* why, std::size_t whyBytes) {
    if (address == nullptr || expected == nullptr || handler == nullptr || label == nullptr || expectedBytes < 8) {
        Explain(why, whyBytes, "bad arguments");
        return false;
    }
    auto* target = static_cast<std::uint8_t*>(address);
    if (std::memcmp(target, expected, expectedBytes) != 0) {
        Explain(why, whyBytes, std::string("bytes differ: found ") + Hex(target, expectedBytes) + ", expected " + Hex(expected, expectedBytes));
        return false;
    }
    std::array<GuestHookInstruction, 8> plan{};
    std::size_t planned = 0;
    std::size_t covered = 0;
    while (covered < JumpBytes) {
        if (planned == plan.size() || !DecodeGuestHookInstruction_nid_no_patch(target + covered, expectedBytes - covered, &plan[planned])) {
            Explain(why, whyBytes, "unsupported instruction at +" + std::to_string(covered) + " (" + Hex(target + covered, std::min<std::size_t>(8, expectedBytes - covered)) + ") or expected bytes too short to cover it");
            return false;
        }
        covered += plan[planned++].length;
    }
    std::lock_guard lock(g_installMutex);
    if (g_block == nullptr) {
        g_block = AllocateNear(reinterpret_cast<std::uintptr_t>(target));
        if (g_block == nullptr) {
            Explain(why, whyBytes, "no executable block within 2 GB of the image");
            return false;
        }
    }
    if (g_blockUsed + SlotBytes > BlockBytes) {
        Explain(why, whyBytes, "hook block is full");
        return false;
    }
    std::uint8_t* slot = g_block + g_blockUsed;
    auto* record = reinterpret_cast<HookRecord*>(slot);
    std::uint8_t* stub = slot + 16;
    std::uint8_t* trampoline = slot + 48;
    // Trampoline: the covered instructions, RIP-relative displacements re-based, then a jump back.
    std::size_t offset = 0;
    for (std::size_t index = 0; index < planned; ++index) {
        const auto& instruction = plan[index];
        std::memcpy(trampoline + offset, target + offset, instruction.length);
        if (instruction.ripDisplacement >= 0) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, target + offset + instruction.ripDisplacement, sizeof(displacement));
            const auto rebased = static_cast<std::int64_t>(displacement) + (reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(trampoline));
            if (!Fits32(rebased)) {
                Explain(why, whyBytes, "RIP-relative operand out of reach from the trampoline");
                return false;
            }
            const auto narrowed = static_cast<std::int32_t>(rebased);
            std::memcpy(trampoline + offset + instruction.ripDisplacement, &narrowed, sizeof(narrowed));
        }
        offset += instruction.length;
    }
    const std::uint8_t jumpBack[] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(trampoline + offset, jumpBack, sizeof(jumpBack));
    Put64(trampoline + offset + sizeof(jumpBack), reinterpret_cast<std::uint64_t>(target + covered));
    // Stub: mov r11, record; jmp GuestHookEnter.
    stub[0] = 0x49;
    stub[1] = 0xbb;
    Put64(stub + 2, reinterpret_cast<std::uint64_t>(record));
    std::memcpy(stub + 10, jumpBack, sizeof(jumpBack));
    Put64(stub + 16, reinterpret_cast<std::uint64_t>(&GuestHookEnter_nid_no_patch));
    const auto relative = reinterpret_cast<std::intptr_t>(stub) - reinterpret_cast<std::intptr_t>(target + JumpBytes);
    if (!Fits32(relative)) {
        Explain(why, whyBytes, "stub out of reach of a 5-byte jump");
        return false;
    }
    auto& state = g_states.emplace_back();
    state.handler = handler;
    state.label = label;
    state.info = {state.handler.c_str(), state.label.c_str(), va, reinterpret_cast<std::uint64_t>(target)};
    state.function.store(FindHandler(handler), std::memory_order_release);
    record->resume = trampoline;
    record->state = &state;
    FlushInstructionCache(GetCurrentProcess(), slot, SlotBytes);
    std::array<std::uint8_t, 16> patch{};
    patch.fill(0xcc);
    patch[0] = 0xe9;
    const auto narrowed = static_cast<std::int32_t>(relative);
    std::memcpy(patch.data() + 1, &narrowed, sizeof(narrowed));
    DWORD old = 0;
    if (!VirtualProtect(target, covered, PAGE_EXECUTE_READWRITE, &old)) {
        g_states.pop_back();
        Explain(why, whyBytes, "VirtualProtect failed");
        return false;
    }
    std::memcpy(target, patch.data(), covered);
    DWORD ignored = 0;
    VirtualProtect(target, covered, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, covered);
    g_blockUsed += SlotBytes;
    if (!g_reporterStarted) {
        g_reporterStarted = true;
        std::thread(Reporter).detach();
    }
    return true;
}

std::uint64_t GuestHookHits_nid_no_patch(const char* label) {
    if (label == nullptr) return 0;
    std::lock_guard lock(g_installMutex);
    for (const auto& state : g_states) {
        if (state.label == label) return state.hits.load(std::memory_order_relaxed);
    }
    return 0;
}

bool ApplyGuestPatch_nid_no_patch(void* address, const std::uint8_t* expected, const std::uint8_t* replacement, std::size_t bytes, char* why, std::size_t whyBytes) {
    if (address == nullptr || expected == nullptr || replacement == nullptr || bytes == 0) {
        Explain(why, whyBytes, "bad arguments");
        return false;
    }
    auto* target = static_cast<std::uint8_t*>(address);
    struct Span {
        std::uint8_t* start;
        std::size_t bytes;
        DWORD protect;
        DWORD previous;
    };
    std::vector<Span> spans;
    for (std::size_t done = 0; done < bytes;) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(target + done, &region, sizeof(region)) != sizeof(region) || region.State != MEM_COMMIT || !Readable(region.Protect)) {
            Explain(why, whyBytes, "the target is not committed readable memory at +" + std::to_string(done));
            return false;
        }
        const auto* regionEnd = static_cast<const std::uint8_t*>(region.BaseAddress) + region.RegionSize;
        const auto length = std::min<std::size_t>(bytes - done, static_cast<std::size_t>(regionEnd - (target + done)));
        spans.push_back({target + done, length, region.Protect, 0});
        done += length;
    }
    if (std::memcmp(target, expected, bytes) != 0) {
        Explain(why, whyBytes, std::string("bytes differ: found ") + Hex(target, bytes) + ", expected " + Hex(expected, bytes));
        return false;
    }
    std::lock_guard lock(g_installMutex);
    std::size_t opened = 0;
    while (opened < spans.size() && VirtualProtect(spans[opened].start, spans[opened].bytes, Executable(spans[opened].protect) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &spans[opened].previous)) ++opened;
    if (opened == spans.size()) std::memcpy(target, replacement, bytes);
    for (std::size_t index = 0; index < opened; ++index) {
        DWORD ignored = 0;
        VirtualProtect(spans[index].start, spans[index].bytes, spans[index].previous, &ignored);
        if (Executable(spans[index].protect)) FlushInstructionCache(GetCurrentProcess(), spans[index].start, spans[index].bytes);
    }
    if (opened != spans.size()) {
        Explain(why, whyBytes, "VirtualProtect failed at +" + std::to_string(spans[opened].start - target));
        return false;
    }
    return true;
}

int FindGuestTargetSection_nid_no_patch(const GuestImageSection* sections, std::size_t count, std::uint64_t va, std::size_t bytes, bool hook, char* why, std::size_t whyBytes) {
    if ((sections == nullptr && count != 0) || bytes == 0) {
        Explain(why, whyBytes, "bad arguments");
        return -1;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& section = sections[index];
        if (va < section.va || va - section.va >= section.size) continue;
        if (bytes > section.size - (va - section.va)) {
            Explain(why, whyBytes, std::string("the range crosses the end of section ") + section.name);
            return -1;
        }
        if (hook && (section.characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            Explain(why, whyBytes, std::string("entry hooks need an executable section, not ") + section.name);
            return -1;
        }
        return static_cast<int>(index);
    }
    Explain(why, whyBytes, hook ? "VA is outside every executable guest section" : "VA is outside every guest section");
    return -1;
}

void APS5_VABI GuestHookDispatch_nid_no_patch(void* record, GuestHookContext* context) {
    auto& state = *static_cast<HookRecord*>(record)->state;
    const auto hit = state.hits.fetch_add(1, std::memory_order_relaxed) + 1;
    auto handler = state.function.load(std::memory_order_acquire);
    if (handler == nullptr) {
        handler = FindHandler(state.handler.c_str());
        if (handler == nullptr) return;
        state.function.store(handler, std::memory_order_release);
    }
    handler(*context, state.info, hit);
}

}

// Entered from a hook stub with r11 = HookRecord* and the stack as at the hooked function's entry
// (rsp = 8 mod 16). Saves the argument registers and ymm0-7 as a GuestHookContext, calls the
// dispatcher, restores them (with any changes the handler made) and continues in the trampoline.
asm(".text\n"
    APS5_ASM_FUNCTION("GuestHookEnter_nid_no_patch")
    "    push %rdi\n"
    "    push %rsi\n"
    "    push %rdx\n"
    "    push %rcx\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %rax\n"
    "    push %r10\n"
    "    push %r11\n"
    "    sub $256, %rsp\n"
    "    vmovdqu %ymm0, 0(%rsp)\n"
    "    vmovdqu %ymm1, 32(%rsp)\n"
    "    vmovdqu %ymm2, 64(%rsp)\n"
    "    vmovdqu %ymm3, 96(%rsp)\n"
    "    vmovdqu %ymm4, 128(%rsp)\n"
    "    vmovdqu %ymm5, 160(%rsp)\n"
    "    vmovdqu %ymm6, 192(%rsp)\n"
    "    vmovdqu %ymm7, 224(%rsp)\n"
    "    mov %r11, %rdi\n"
    "    mov %rsp, %rsi\n"
    "    call GuestHookDispatch_nid_no_patch\n"
    "    vmovdqu 0(%rsp), %ymm0\n"
    "    vmovdqu 32(%rsp), %ymm1\n"
    "    vmovdqu 64(%rsp), %ymm2\n"
    "    vmovdqu 96(%rsp), %ymm3\n"
    "    vmovdqu 128(%rsp), %ymm4\n"
    "    vmovdqu 160(%rsp), %ymm5\n"
    "    vmovdqu 192(%rsp), %ymm6\n"
    "    vmovdqu 224(%rsp), %ymm7\n"
    "    add $256, %rsp\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %rax\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rcx\n"
    "    pop %rdx\n"
    "    pop %rsi\n"
    "    pop %rdi\n"
    "    jmp *(%r11)\n");
#endif

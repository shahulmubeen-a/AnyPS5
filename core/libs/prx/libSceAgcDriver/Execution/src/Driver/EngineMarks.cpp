#include "prx/libSceAgcDriver/Execution/include/Driver/EngineMarks.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "prx/libc/include/GuestHooks.hpp"
#include "prx/libc/include/general/AsmMacros.hpp"
#endif

namespace AgcDriver::EngineMarks {

namespace {

using Counters = std::array<std::atomic<std::uint64_t>, KindCount>;

struct Totals {
    // Written by the hook handlers (guest threads).
    Counters written{};
    Counters skipped{};
    std::atomic<std::uint64_t> exitsUnmatched{0};
    // Written by the queue workers.
    Counters seen{};
    Counters firstDraws{};
    Counters extraDraws{};
    Counters zeroDraw{};
    Counters oneDraw{};
    Counters manyDraws{};
    Counters unterminated{};
    std::atomic<std::uint64_t> draws{0};
    std::atomic<std::uint64_t> outsideDirect{0};
    std::atomic<std::uint64_t> outsideIndirect{0};
};

Totals totals;

const char* const KindNames[KindCount] = {"?", "item", "instanced", "instanced-b", "material", "item-end", "instanced-end", "instanced-b-end", "indirect", "indirect-end", "other", "other-end"};

bool EndKind(Kind kind) {
    return kind == ItemEnd || kind == InstancedEnd || kind == InstancedBEnd || kind == IndirectEnd || kind == OtherEnd;
}

struct OtherStats {
    std::uint64_t calls = 0;
    std::uint64_t draws = 0;
    std::uint64_t oneDraw = 0;
};

std::mutex otherMutex;
std::map<std::uint64_t, OtherStats> otherStats;

// Probe 6: per engine draw key (technique, geometry, material or part), the packet stream between begin and
// end, compared with the previous call on the same key.
struct StreamRecord {
    std::uint64_t hash = 0;
    std::vector<std::uint32_t> dwords;
    std::vector<std::uint32_t> packetOffsets;
    // Probe 7: the register lists the stream's indirect-register packets point at (offset, value pairs) and the
    // first 16 dwords behind the user-data pointer at SH 0x8c/0x8d, read when the packets are seen.
    std::vector<std::uint32_t> contents;
};

struct StreamTotals {
    std::uint64_t newKeys = 0;
    std::uint64_t repeatsSame = 0;
    std::uint64_t repeatsDiffer = 0;
    std::uint64_t repeatsDifferentShape = 0;
    std::uint64_t dwords = 0;
    std::uint64_t dwordsDiffering = 0;
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> packetsByOpcode;  // compared, differing
    std::map<std::uint32_t, std::uint64_t> differingRegisters;                           // register offset -> count
    std::uint64_t contentDwords = 0;
    std::uint64_t contentDiffering = 0;
    std::uint64_t contentRepeatsSame = 0;
    std::uint64_t contentRepeatsDiffer = 0;
    std::uint64_t contentShapeDiffer = 0;
    std::map<std::uint32_t, std::uint64_t> differingListRegisters;                       // register offset inside lists -> count
    std::uint64_t userBlockDwordsDiffering = 0;
};

std::mutex streamMutex;
std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>, StreamRecord> streams;
StreamTotals streamTotals;

std::uint64_t Fnv(const std::vector<std::uint32_t>& dwords) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto dword : dwords) {
        hash ^= dword;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::mutex foreignMutex;
std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> foreignPrograms;
std::mutex sitesMutex;
std::map<std::uint64_t, std::uint64_t> foreignSites;
std::atomic<std::uint64_t> engineSiteDraws{0};

#ifdef _WIN32
// app.exe's .elf0 section start: eboot VA = host address - elf0.
std::uintptr_t Elf0() {
    static const std::uintptr_t elf0 = [] {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* first = IMAGE_FIRST_SECTION(nt);
        for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
            if (std::memcmp(first[index].Name, ".elf0\0\0\0", 8) == 0) return base + first[index].VirtualAddress;
        }
        return std::uintptr_t{0};
    }();
    return elf0;
}
#endif

std::uint64_t Load64(std::uint64_t address) {
    std::uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

#ifdef _WIN32
// Writes a marker into the engine context's DCB; `technique` fills the first value.
void Emit(std::uint64_t ctx, Kind kind, std::uint64_t technique, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t hit) {
    if (ctx == 0) {
        ++totals.skipped[kind];
        return;
    }
    auto* cursor = reinterpret_cast<std::uint32_t*>(Load64(ctx + 0x10));
    const auto* end = reinterpret_cast<const std::uint32_t*>(Load64(ctx + 0x18));
    std::uint32_t reserved = 0;
    std::memcpy(&reserved, reinterpret_cast<const void*>(ctx + 0x30), sizeof(reserved));
    if (cursor == nullptr || end <= cursor || static_cast<std::uint64_t>(end - cursor) < MarkerDwords + static_cast<std::uint64_t>(reserved)) {
        ++totals.skipped[kind];
        return;
    }
    cursor[0] = (3u << 30u) | ((MarkerDwords - 2u) << 16u) | (0x10u << 8u);
    cursor[1] = MarkerMagic | kind;
    const std::uint64_t values[4] = {technique, a, b, c};
    std::memcpy(cursor + 2, values, sizeof(values));
    cursor[10] = static_cast<std::uint32_t>(hit);
    const auto next = reinterpret_cast<std::uint64_t>(cursor + MarkerDwords);
    std::memcpy(reinterpret_cast<void*>(ctx + 0x10), &next, sizeof(next));
    ++totals.written[kind];
}

// Return-through-stub bookkeeping: each hooked draw call pushes its real return address and context here
// and returns into L3ExitStub, which calls Exit() and jumps to the real return address.
struct OpenCall {
    std::uint64_t returnAddress;
    std::uint64_t ctx;
    std::uint64_t technique;
    std::uint64_t hit;
    Kind endKind;
};

thread_local std::vector<OpenCall> openCalls;

extern "C" void L3ExitStub_nid_no_patch();

void Enter(GuestHookContext& context, Kind kind, Kind endKind, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t hit) {
    Emit(context.rsi, kind, context.rdi, a, b, c, hit);
    openCalls.push_back({context.returnAddress, context.rsi, context.rdi, hit, endKind});
    context.returnAddress = reinterpret_cast<std::uint64_t>(&L3ExitStub_nid_no_patch);
}

// DrawItem(technique, ctx, item): item+0x18 points at {geometry, material}.
void ItemHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    const auto item = context.rdx;
    const auto part = item != 0 ? Load64(item + 0x18) : 0;
    Enter(context, Item, ItemEnd, item, part != 0 ? Load64(part) : 0, part != 0 ? Load64(part + 8) : 0, hit);
}

// DrawInstanced(technique, ctx, instances, geometry, part, flags).
void InstancedHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Enter(context, Instanced, InstancedEnd, context.rdx, context.rcx, context.r8, hit);
}

void InstancedBHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Enter(context, InstancedB, InstancedBEnd, context.rdx, context.rcx, context.r8, hit);
}

// GPU-driven indirect draw wrappers: the second argument is assumed to be the context, as for the others.
void IndirectHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Enter(context, Indirect, IndirectEnd, context.rdx, context.rcx, context.r8, hit);
}

// Any other engine draw function whose second argument is the context (checked in the decompiled body).
void OtherHandler(GuestHookContext& context, const GuestHookInfo& info, std::uint64_t hit) {
    Enter(context, Other, OtherEnd, info.va, context.rdx, context.rcx, hit);
}

// BindMaterial(technique, ctx, material, binding record): begin marker only.
void MaterialHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Emit(context.rsi, Material, context.rdi, context.rdx, context.rcx, 0, hit);
}

}

// Called by L3ExitStub on the guest thread after the hooked function returned; returns the real return address.
extern "C" std::uint64_t L3Exit_nid_no_patch() {
    if (openCalls.empty()) {
        ++totals.exitsUnmatched;
        std::fputs("[l3] exit stub without an open call: cannot continue\n", stderr);
        std::fflush(stderr);
        std::abort();
    }
    const auto call = openCalls.back();
    openCalls.pop_back();
    Emit(call.ctx, call.endKind, call.technique, 0, 0, 0, call.hit);
    return call.returnAddress;
}

// Entered by `ret` from the hooked function, so rsp is 16-byte aligned and the return values are live.
// Preserves rax, rdx, xmm0 and xmm1 (SysV return registers); the callee-saved registers survive the MS-ABI
// call to L3Exit on their own.
asm(".text\n"
    APS5_ASM_FUNCTION("L3ExitStub_nid_no_patch")
    "    push %rax\n"
    "    push %rdx\n"
    "    push %rcx\n"
    "    sub $72, %rsp\n"
    "    vmovdqu %xmm0, 0(%rsp)\n"
    "    vmovdqu %xmm1, 16(%rsp)\n"
    "    call L3Exit_nid_no_patch\n"
    "    mov %rax, %r11\n"
    "    vmovdqu 0(%rsp), %xmm0\n"
    "    vmovdqu 16(%rsp), %xmm1\n"
    "    add $72, %rsp\n"
    "    pop %rcx\n"
    "    pop %rdx\n"
    "    pop %rax\n"
    "    jmp *%r11\n");

namespace {
#endif

std::string Row(const char* name, const Counters& counters, std::array<std::uint64_t, KindCount>& last, bool endsToo) {
    std::string text = std::string(" ") + name;
    for (std::uint32_t kind = 1; kind < KindCount; ++kind) {
        if (!endsToo && EndKind(static_cast<Kind>(kind))) continue;
        const auto value = counters[kind].load(std::memory_order_relaxed);
        text += " " + std::string(KindNames[kind]) + "=" + std::to_string(value - last[kind]);
        last[kind] = value;
    }
    return text + ";";
}

void Reporter() {
    std::array<std::array<std::uint64_t, KindCount>, 9> last{};
    std::uint64_t lastDraws = 0, lastDirect = 0, lastIndirect = 0, lastUnmatched = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        const auto draws = totals.draws.load(std::memory_order_relaxed);
        const auto direct = totals.outsideDirect.load(std::memory_order_relaxed);
        const auto indirect = totals.outsideIndirect.load(std::memory_order_relaxed);
        const auto unmatched = totals.exitsUnmatched.load(std::memory_order_relaxed);
        std::string text = "[l3] (10 s): draws " + std::to_string(draws - lastDraws) + ", foreign " + std::to_string(direct - lastDirect) + " direct + " + std::to_string(indirect - lastIndirect) + " indirect, unmatched exits " + std::to_string(unmatched - lastUnmatched) + ";";
        lastDraws = draws;
        lastDirect = direct;
        lastIndirect = indirect;
        lastUnmatched = unmatched;
        text += Row("written", totals.written, last[0], true);
        text += Row("skipped", totals.skipped, last[1], true);
        text += Row("seen", totals.seen, last[2], true);
        text += Row("first-draws", totals.firstDraws, last[3], false);
        text += Row("extra-draws", totals.extraDraws, last[4], false);
        text += Row("calls-0-draws", totals.zeroDraw, last[5], false);
        text += Row("calls-1-draw", totals.oneDraw, last[6], false);
        text += Row("calls-2+-draws", totals.manyDraws, last[7], false);
        text += Row("unterminated", totals.unterminated, last[8], false);
        text += "\n";
        std::vector<std::pair<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>>> hot;
        {
            std::lock_guard lock(foreignMutex);
            for (const auto& [key, count] : foreignPrograms) hot.emplace_back(count, key);
            foreignPrograms.clear();
        }
        if (!hot.empty()) {
            std::sort(hot.begin(), hot.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            text += "[l3-foreign] (10 s) draws by program set (first/last):";
            char item[80];
            for (std::size_t index = 0; index < std::min<std::size_t>(hot.size(), 24); ++index) {
                std::snprintf(item, sizeof(item), " 0x%llx/0x%llx %llu;", static_cast<unsigned long long>(hot[index].second.first), static_cast<unsigned long long>(hot[index].second.second), static_cast<unsigned long long>(hot[index].first));
                text += item;
            }
            text += " (" + std::to_string(hot.size()) + " sets)\n";
        }
        std::vector<std::pair<std::uint64_t, std::uint64_t>> sites;
        {
            std::lock_guard lock(sitesMutex);
            for (const auto& [site, count] : foreignSites) sites.emplace_back(count, site);
            foreignSites.clear();
        }
        if (!sites.empty()) {
            std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            text += "[l3-sites] (10 s) guest draw call sites outside engine draw calls (eboot VA of the return address):";
            char item[64];
            for (std::size_t index = 0; index < std::min<std::size_t>(sites.size(), 32); ++index) {
                std::snprintf(item, sizeof(item), " 0x%llx %llu;", static_cast<unsigned long long>(sites[index].second), static_cast<unsigned long long>(sites[index].first));
                text += item;
            }
            text += " (" + std::to_string(sites.size()) + " sites; inside engine calls " + std::to_string(engineSiteDraws.exchange(0)) + ")\n";
        }
        std::vector<std::pair<std::uint64_t, OtherStats>> others;
        {
            std::lock_guard lock(otherMutex);
            for (const auto& [va, stats] : otherStats) others.emplace_back(va, stats);
            otherStats.clear();
        }
        if (!others.empty()) {
            std::sort(others.begin(), others.end(), [](const auto& a, const auto& b) { return a.second.draws > b.second.draws; });
            text += "[l3-other] (10 s) other hooked engine functions, VA calls/draws/calls-with-1-draw:";
            char item[96];
            for (const auto& [va, stats] : others) {
                std::snprintf(item, sizeof(item), " 0x%llx %llu/%llu/%llu;", static_cast<unsigned long long>(va), static_cast<unsigned long long>(stats.calls), static_cast<unsigned long long>(stats.draws), static_cast<unsigned long long>(stats.oneDraw));
                text += item;
            }
            text += "\n";
        }
        {
            std::lock_guard lock(streamMutex);
            auto& t = streamTotals;
            const auto repeats = t.repeatsSame + t.repeatsDiffer + t.repeatsDifferentShape;
            if (repeats + t.newKeys > 0) {
                text += "[l3-stream] (10 s) packet streams per engine draw key: new keys " + std::to_string(t.newKeys) + ", repeats identical " + std::to_string(t.repeatsSame) + ", differing " + std::to_string(t.repeatsDiffer) + ", different shape " + std::to_string(t.repeatsDifferentShape) + "; dwords per stream " + std::to_string(repeats + t.newKeys > 0 ? t.dwords / (repeats + t.newKeys) : 0) + ", differing dwords " + std::to_string(t.dwordsDiffering) + " of " + std::to_string(t.dwords) + "; packets compared/differing by opcode:";
                char item[96];
                for (const auto& [opcode, counts] : t.packetsByOpcode) {
                    std::snprintf(item, sizeof(item), " %02x %llu/%llu;", opcode, static_cast<unsigned long long>(counts.first), static_cast<unsigned long long>(counts.second));
                    text += item;
                }
                std::vector<std::pair<std::uint64_t, std::uint32_t>> regs;
                for (const auto& [reg, count] : t.differingRegisters) regs.emplace_back(count, reg);
                std::sort(regs.begin(), regs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                text += " differing registers (offset count):";
                for (std::size_t index = 0; index < std::min<std::size_t>(regs.size(), 32); ++index) {
                    std::snprintf(item, sizeof(item), " %x %llu;", regs[index].second, static_cast<unsigned long long>(regs[index].first));
                    text += item;
                }
                text += " (" + std::to_string(regs.size()) + " registers, " + std::to_string(streams.size()) + " keys held)\n";
                text += "[l3-contents] (10 s) register-list and user-block contents per key: repeats identical " + std::to_string(t.contentRepeatsSame) + ", differing " + std::to_string(t.contentRepeatsDiffer) + ", different size " + std::to_string(t.contentShapeDiffer) + "; dwords " + std::to_string(t.contentDwords) + ", differing " + std::to_string(t.contentDiffering) + " (user block " + std::to_string(t.userBlockDwordsDiffering) + "); differing list registers (offset count):";
                std::vector<std::pair<std::uint64_t, std::uint32_t>> listRegs;
                for (const auto& [reg, count] : t.differingListRegisters) listRegs.emplace_back(count, reg);
                std::sort(listRegs.begin(), listRegs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                for (std::size_t index = 0; index < std::min<std::size_t>(listRegs.size(), 40); ++index) {
                    std::snprintf(item, sizeof(item), " %x %llu;", listRegs[index].second, static_cast<unsigned long long>(listRegs[index].first));
                    text += item;
                }
                text += " (" + std::to_string(listRegs.size()) + " registers)\n";
                t = {};
            }
        }
        std::fputs(text.c_str(), stderr);
        std::fflush(stderr);
    }
}

const bool enabled = [] {
    if (std::getenv("APS5_PORTER_L3") == nullptr) return false;
#ifdef _WIN32
    RegisterGuestHookHandler_nid_no_patch("l3-item", &ItemHandler);
    RegisterGuestHookHandler_nid_no_patch("l3-instanced", &InstancedHandler);
    RegisterGuestHookHandler_nid_no_patch("l3-instanced-b", &InstancedBHandler);
    RegisterGuestHookHandler_nid_no_patch("l3-indirect", &IndirectHandler);
    RegisterGuestHookHandler_nid_no_patch("l3-material", &MaterialHandler);
    RegisterGuestHookHandler_nid_no_patch("l3-other", &OtherHandler);
#endif
    std::thread(Reporter).detach();
    return true;
}();

// One queue worker thread processes a submission at a time; open calls never span submissions.
struct WorkerState {
    std::uint32_t queue = 0xffffffffu;
    std::uint64_t serial = 0;
    Kind open = Kind(0);
    std::uint64_t openVa = 0;
    std::uint64_t drawsInside = 0;
    bool lastDrawForeign = false;
    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t> key{};
    std::vector<std::uint32_t> stream;
    std::vector<std::uint32_t> packetOffsets;
    std::vector<std::uint32_t> contents;
};

thread_local WorkerState worker;

// Compares the closed call's packet stream with the previous call on the same key and records the differences.
void CompareStream() {
    std::lock_guard lock(streamMutex);
    auto& t = streamTotals;
    t.dwords += worker.stream.size();
    auto& record = streams[worker.key];
    const auto hash = Fnv(worker.stream);
    if (record.dwords.empty() && record.hash == 0) {
        ++t.newKeys;
    } else if (hash == record.hash && record.dwords == worker.stream) {
        ++t.repeatsSame;
    } else if (record.packetOffsets != worker.packetOffsets) {
        ++t.repeatsDifferentShape;
    } else {
        ++t.repeatsDiffer;
        for (std::size_t p = 0; p < worker.packetOffsets.size(); ++p) {
            const auto start = worker.packetOffsets[p];
            const auto stop = p + 1 < worker.packetOffsets.size() ? worker.packetOffsets[p + 1] : static_cast<std::uint32_t>(worker.stream.size());
            const auto opcode = (worker.stream[start] >> 8u) & 0xffu;
            auto& counts = t.packetsByOpcode[opcode];
            ++counts.first;
            bool differs = false;
            for (auto i = start; i < stop; ++i) {
                if (worker.stream[i] == record.dwords[i]) continue;
                differs = true;
                ++t.dwordsDiffering;
                if ((opcode == 0x69 || opcode == 0x76 || opcode == 0x79 || opcode == 0x7a) && i >= start + 2) ++t.differingRegisters[(worker.stream[start + 1] & 0xffffu) + (i - start - 2)];
            }
            if (differs) ++counts.second;
        }
    }
    record.hash = hash;
    record.dwords = worker.stream;
    record.packetOffsets = worker.packetOffsets;
    t.contentDwords += worker.contents.size();
    if (!record.contents.empty() || record.hash != hash) {
        if (record.contents.size() != worker.contents.size()) {
            ++t.contentShapeDiffer;
        } else if (record.contents == worker.contents) {
            ++t.contentRepeatsSame;
        } else {
            ++t.contentRepeatsDiffer;
            for (std::size_t i = 0; i < worker.contents.size(); ++i) {
                if (worker.contents[i] == record.contents[i]) continue;
                ++t.contentDiffering;
                // Pairs come as (offset, value); the user block is tagged with offset 0xffff0000.
                if (i % 2 == 1) {
                    const auto tag = worker.contents[i - 1];
                    if (tag == 0xffff0000u) ++t.userBlockDwordsDiffering;
                    else ++t.differingListRegisters[tag];
                }
            }
        }
    }
    record.contents = worker.contents;
}

void Close(bool terminated) {
    if (worker.open == 0) return;
    if (terminated && (worker.open == Item || worker.open == Instanced) && !worker.stream.empty()) CompareStream();
    worker.stream.clear();
    worker.packetOffsets.clear();
    worker.contents.clear();
    auto& row = worker.drawsInside == 0 ? totals.zeroDraw : worker.drawsInside == 1 ? totals.oneDraw : totals.manyDraws;
    ++row[worker.open];
    if (!terminated) ++totals.unterminated[worker.open];
    if (worker.open == Other) {
        std::lock_guard lock(otherMutex);
        auto& stats = otherStats[worker.openVa];
        ++stats.calls;
        stats.draws += worker.drawsInside;
        if (worker.drawsInside == 1) ++stats.oneDraw;
    }
    worker.open = Kind(0);
    worker.openVa = 0;
    worker.drawsInside = 0;
}

}

bool Enabled() {
    return enabled;
}

bool IsMarker(std::span<const std::uint32_t> packet) {
    return packet.size() == MarkerDwords && ((packet[0] >> 8u) & 0xffu) == 0x10u && ((packet[0] >> 2u) & 0x3fu) == 0 && (packet[1] & 0xffff0000u) == MarkerMagic && (packet[1] & 0xffffu) > 0 && (packet[1] & 0xffffu) < KindCount;
}

void Note(std::uint32_t queue, std::uint64_t serial, std::uint32_t, std::uint32_t opcode, std::span<const std::uint32_t> packet) {
    if (!enabled) return;
    if (worker.queue != queue || worker.serial != serial) {
        Close(false);
        worker.queue = queue;
        worker.serial = serial;
    }
    if (opcode == 0x10 && IsMarker(packet)) {
        const auto kind = static_cast<Kind>(packet[1] & 0xffffu);
        ++totals.seen[kind];
        if (kind == Material) return;
        if (EndKind(kind)) {
            Close(true);
            return;
        }
        Close(false);
        worker.open = kind;
        if (kind == Other) std::memcpy(&worker.openVa, &packet[4], sizeof(worker.openVa));
        if (kind == Item || kind == Instanced) {
            // Item: values = technique, item, geometry, material. Instanced: technique, instances, geometry, part.
            std::uint64_t values[4];
            std::memcpy(values, &packet[2], sizeof(values));
            worker.key = {values[0], values[2], values[3]};
        }
        return;
    }
    if (worker.open == Item || worker.open == Instanced) {
        worker.packetOffsets.push_back(static_cast<std::uint32_t>(worker.stream.size()));
        worker.stream.insert(worker.stream.end(), packet.begin(), packet.end());
        if ((opcode == 0x63 || opcode == 0x64 || opcode == 0x9f) && packet.size() == 5) {
            const auto address = packet[1] | (static_cast<std::uint64_t>(packet[2]) << 32u);
            const auto bytes = static_cast<std::size_t>(packet[4]) * (packet[3] == 0x80000000u ? 8u : 4u);
            if (GuestMemory::Accessible(reinterpret_cast<const void*>(address), bytes)) {
                const auto pairs = Pm4::ReadIndirectRegisters(packet);
                worker.contents.insert(worker.contents.end(), pairs.begin(), pairs.end());
            }
        }
        if (opcode == 0x76 && packet.size() >= 4 && (packet[1] & 0xffffu) == 0x8c) {
            const auto pointer = packet[2] | (static_cast<std::uint64_t>(packet[3]) << 32u);
            if (pointer != 0 && GuestMemory::Accessible(reinterpret_cast<const void*>(pointer), 64)) {
                std::uint32_t block[16];
                std::memcpy(block, reinterpret_cast<const void*>(pointer), sizeof(block));
                for (const auto dword : block) {
                    worker.contents.push_back(0xffff0000u);
                    worker.contents.push_back(dword);
                }
            }
        }
    }
    if (!Pm4::DrawOpcode(opcode)) return;
    ++totals.draws;
    worker.lastDrawForeign = worker.open == 0;
    if (worker.open == 0) {
        ++(Pm4::IndirectDrawOpcode(opcode) ? totals.outsideIndirect : totals.outsideDirect);
        return;
    }
    ++(worker.drawsInside == 0 ? totals.firstDraws : totals.extraDraws)[worker.open];
    ++worker.drawsInside;
}

void NoteDrawPrograms(std::uint64_t first, std::uint64_t last) {
    if (!enabled || !worker.lastDrawForeign) return;
    std::lock_guard lock(foreignMutex);
    ++foreignPrograms[{first, last}];
}


extern "C" void L3NoteGuestDraw_nid_no_patch(const void* returnAddress) {
    if (!Enabled()) return;
#ifdef _WIN32
    if (!openCalls.empty()) {
        ++engineSiteDraws;
        return;
    }
    const auto host = reinterpret_cast<std::uintptr_t>(returnAddress);
    const auto elf0 = Elf0();
    const auto va = elf0 != 0 && host >= elf0 ? host - elf0 : host;
    std::lock_guard lock(sitesMutex);
    ++foreignSites[va];
#endif
}

}

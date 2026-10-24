#include "prx/libSceAgcDriver/Execution/include/Driver/EngineMarks.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#include "prx/libc/include/GuestHooks.hpp"
#endif

namespace AgcDriver::EngineMarks {

namespace {

using Counters = std::array<std::atomic<std::uint64_t>, KindCount>;

struct Totals {
    // Written by the hook handlers (guest threads).
    Counters written{};
    Counters skipped{};
    // Written by the queue workers.
    Counters seen{};
    Counters firstDraws{};
    Counters extraDraws{};
    Counters zeroDraw{};
    Counters oneDraw{};
    Counters manyDraws{};
    std::atomic<std::uint64_t> draws{0};
    std::atomic<std::uint64_t> outsideDirect{0};
    std::atomic<std::uint64_t> outsideIndirect{0};
};

Totals totals;

const char* const KindNames[KindCount] = {"?", "item", "instanced", "instanced-b", "material"};

std::uint64_t Load64(std::uint64_t address) {
    std::uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

#ifdef _WIN32
void Emit(GuestHookContext& context, Kind kind, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t hit) {
    const auto ctx = context.rsi;
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
    const std::uint64_t values[4] = {context.rdi, a, b, c};
    std::memcpy(cursor + 2, values, sizeof(values));
    cursor[10] = static_cast<std::uint32_t>(hit);
    const auto next = reinterpret_cast<std::uint64_t>(cursor + MarkerDwords);
    std::memcpy(reinterpret_cast<void*>(ctx + 0x10), &next, sizeof(next));
    ++totals.written[kind];
}

// DrawItem(technique, ctx, item): item+0x18 points at {geometry, material}.
void ItemHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    const auto item = context.rdx;
    const auto part = item != 0 ? Load64(item + 0x18) : 0;
    Emit(context, Item, item, part != 0 ? Load64(part) : 0, part != 0 ? Load64(part + 8) : 0, hit);
}

// DrawInstanced(technique, ctx, instances, geometry, part, flags).
void InstancedHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Emit(context, Instanced, context.rdx, context.rcx, context.r8, hit);
}

void InstancedBHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Emit(context, InstancedB, context.rdx, context.rcx, context.r8, hit);
}

// BindMaterial(technique, ctx, material, binding record).
void MaterialHandler(GuestHookContext& context, const GuestHookInfo&, std::uint64_t hit) {
    Emit(context, Material, context.rdx, context.rcx, 0, hit);
}
#endif

std::string Row(const char* name, const Counters& counters, std::array<std::uint64_t, KindCount>& last) {
    std::string text = std::string(" ") + name;
    for (std::uint32_t kind = 1; kind < KindCount; ++kind) {
        const auto value = counters[kind].load(std::memory_order_relaxed);
        text += " " + std::string(KindNames[kind]) + "=" + std::to_string(value - last[kind]);
        last[kind] = value;
    }
    return text + ";";
}

void Reporter() {
    std::array<std::array<std::uint64_t, KindCount>, 8> last{};
    std::uint64_t lastDraws = 0, lastDirect = 0, lastIndirect = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        const auto draws = totals.draws.load(std::memory_order_relaxed);
        const auto direct = totals.outsideDirect.load(std::memory_order_relaxed);
        const auto indirect = totals.outsideIndirect.load(std::memory_order_relaxed);
        std::string text = "[l3] (10 s): draws " + std::to_string(draws - lastDraws) + ", outside markers " + std::to_string(direct - lastDirect) + " direct + " + std::to_string(indirect - lastIndirect) + " indirect;";
        lastDraws = draws;
        lastDirect = direct;
        lastIndirect = indirect;
        text += Row("written", totals.written, last[0]);
        text += Row("skipped", totals.skipped, last[1]);
        text += Row("seen", totals.seen, last[2]);
        text += Row("first-draws", totals.firstDraws, last[3]);
        text += Row("extra-draws", totals.extraDraws, last[4]);
        text += Row("markers-0-draws", totals.zeroDraw, last[5]);
        text += Row("markers-1-draw", totals.oneDraw, last[6]);
        text += Row("markers-2+-draws", totals.manyDraws, last[7]);
        text += "\n";
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
    RegisterGuestHookHandler_nid_no_patch("l3-material", &MaterialHandler);
#endif
    std::thread(Reporter).detach();
    return true;
}();

// One queue worker thread processes a submission at a time; markers never span submissions.
struct WorkerState {
    std::uint32_t queue = 0xffffffffu;
    std::uint64_t serial = 0;
    Kind pending = Kind(0);
    std::uint64_t drawsAfter = 0;
};

thread_local WorkerState worker;

void Finish() {
    if (worker.pending == 0) return;
    auto& row = worker.drawsAfter == 0 ? totals.zeroDraw : worker.drawsAfter == 1 ? totals.oneDraw : totals.manyDraws;
    ++row[worker.pending];
    worker.pending = Kind(0);
    worker.drawsAfter = 0;
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
        Finish();
        worker.queue = queue;
        worker.serial = serial;
    }
    if (opcode == 0x10 && IsMarker(packet)) {
        const auto kind = static_cast<Kind>(packet[1] & 0xffffu);
        ++totals.seen[kind];
        if (kind == Material) return;
        Finish();
        worker.pending = kind;
        return;
    }
    if (!Pm4::DrawOpcode(opcode)) return;
    ++totals.draws;
    if (worker.pending == 0) {
        ++(Pm4::IndirectDrawOpcode(opcode) ? totals.outsideIndirect : totals.outsideDirect);
        return;
    }
    ++(worker.drawsAfter == 0 ? totals.firstDraws : totals.extraDraws)[worker.pending];
    ++worker.drawsAfter;
}

}

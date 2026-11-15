#include "prx/libSceAgc/DcbState/include/Marker.hpp"

#include "prx/libSceAgc/Command/include/Packet.hpp"
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"

// Debug markers are custom NOP packets: PUSH carries the NUL-terminated text, POP has no payload.
// Marker colors are only meaningful to capture tools and are not encoded.
// APS5_NO_AGC_MARKERS=1 encodes no markers at all: Push, PushSpan and Pop allocate nothing and return the cursor.
// Ghost of Yotei (PPSA26344) pops a marker from one thread through a command buffer copy whose cursor
// another thread has already written past; the 2-word POP lands over the first words of that thread's
// packet and the submission no longer parses (an early-boot "unsupported PM4 packet type").
namespace {

constexpr std::uint32_t OpcodeNop = 0x10;
constexpr std::uint32_t CustomPushMarker = 0x0b;
constexpr std::uint32_t CustomPopMarker = 0x0c;

bool MarkersEncoded() {
    static const bool encoded = std::getenv("APS5_NO_AGC_MARKERS") == nullptr;
    return encoded;
}

// The race above, kept from the stream: a marker asked for through a command buffer copy whose cursor lies behind
// the latest allocation in the same storage is not encoded (it would land over packets written since the copy was
// taken). The marker stack then misses that push or pop; the packets stay whole. APS5_STALE_MARKERS=1 encodes them
// as before. A copy still at the storage's start is not told from a reset buffer and encodes.
bool StaleCopy(const CommandBuffer* buf, const char* function) {
    static const bool check = std::getenv("APS5_STALE_MARKERS") == nullptr;
    if (!check || !Agc::Command::CursorBehindLatestAllocation(buf)) return false;
    static std::atomic<std::uint32_t> reports{0};
    const auto count = reports.fetch_add(1, std::memory_order_relaxed) + 1u;
    if (count <= 4u || count % 4096u == 0u) std::fprintf(stderr, "[agc] %s through a command buffer copy with a cursor behind the storage's latest packet: marker not encoded (%u so far)\n", function, count);
    return true;
}

}

namespace Agc::Marker {

std::uint32_t* Push(CommandBuffer* buf, const char* str, const char* function) {
    Agc::Command::Require(buf != nullptr, function, "null command buffer");
    if (!MarkersEncoded()) return buf->cursor_up;
    const char* text = str ? str : "";
    const std::size_t length = std::strlen(text);
    Agc::Command::Require(length < 0x10000u, function, "marker text too long");
    return PushSpan(buf, text, static_cast<std::uint32_t>(length), function);
}

std::uint32_t* PushSpan(CommandBuffer* buf, const char* str, std::uint32_t length, const char* function) {
    Agc::Command::Require(buf != nullptr, function, "null command buffer");
    Agc::Command::Require(str != nullptr || length == 0, function, "null marker text");
    if (!MarkersEncoded() || StaleCopy(buf, function)) return buf->cursor_up;
    const std::size_t bytes = std::size_t{length} + 1;
    const auto payload = static_cast<std::uint32_t>((bytes + 3) / 4);
    Agc::Command::Require(payload <= 0x4000u, function, "marker text too long");
    auto* packet = Agc::Command::Allocate(buf, payload + 1, function);
    packet[0] = Agc::Command::Header(OpcodeNop, payload + 1, CustomPushMarker << 2);
    std::memset(packet + 1, 0, payload * sizeof(std::uint32_t));
    if (length != 0) std::memcpy(packet + 1, str, length);
    return packet;
}

std::uint32_t* Pop(CommandBuffer* buf, const char* function) {
    Agc::Command::Require(buf != nullptr, function, "null command buffer");
    if (!MarkersEncoded() || StaleCopy(buf, function)) return buf->cursor_up;
    auto* packet = Agc::Command::Allocate(buf, 2, function);
    packet[0] = Agc::Command::Header(OpcodeNop, 2, CustomPopMarker << 2);
    packet[1] = 0;
    return packet;
}

}

extern "C" {

uint32_t* APS5_VABI sceAgcDcbSetMarker(CommandBuffer* buf, const char* str, uint32_t color) {
    (void)color;
    auto* packet = Agc::Marker::Push(buf, str, __func__);
    Agc::Marker::Pop(buf, __func__);
    return packet;
}

uint32_t* APS5_VABI sceAgcDcbPopMarker(CommandBuffer* buf) {
    return Agc::Marker::Pop(buf, __func__);
}

uint32_t* APS5_VABI sceAgcDcbPushMarker(CommandBuffer* buf, const char* str, uint32_t color) {
    (void)color;
    return Agc::Marker::Push(buf, str, __func__);
}

uint32_t* APS5_VABI sceAgcDcbPushMarkerSpan(CommandBuffer* buf, const char* str, uint32_t length, uint32_t color) {
    (void)color;
    return Agc::Marker::PushSpan(buf, str, length, __func__);
}

uint32_t* APS5_VABI sceAgcDcbSetMarkerSpan(CommandBuffer* buf, const char* str, uint32_t length, uint32_t color) {
    (void)color;
    auto* packet = Agc::Marker::PushSpan(buf, str, length, __func__);
    Agc::Marker::Pop(buf, __func__);
    return packet;
}

}

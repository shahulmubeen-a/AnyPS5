#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_ENGINEMARKS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_ENGINEMARKS_HPP

#include <cstdint>
#include <span>

// Porter L3 probe (APS5_PORTER_L3=1, Windows): guest-hook handlers for the game engine's renderer functions
// (DrawItem, DrawInstanced, BindMaterial; listed in a hook table such as porter/hooks/PPSA26344-l3marks.hooks)
// write one type-3 NOP into the engine context's command buffer before the function runs:
//   NOP (sub-op 0, 11 dwords): 0x4c33'0000 | kind, then four 64-bit values (technique, a, b, c).
// The context is the hooked function's second argument; its command buffer cursor is at +0x10 and its end at
// +0x18, with the engine's reserved tail (dwords) at +0x30. A marker is written only when it fits before the
// end and the reserved tail, because a handler must not call the engine's reserve callback (guest code).
// The packet loop passes every packet to Note(); it attributes each draw packet to the engine marker that
// precedes it in the same submission and prints "[l3]" totals every 10 s.
namespace AgcDriver::EngineMarks {

constexpr std::uint32_t MarkerMagic = 0x4c330000u;
constexpr std::uint32_t MarkerDwords = 11;

enum Kind : std::uint32_t { Item = 1, Instanced = 2, InstancedB = 3, Material = 4, KindCount = 5 };

bool Enabled();
// Called by the queue worker for every packet; cheap when disabled.
void Note(std::uint32_t queue, std::uint64_t serial, std::uint32_t header, std::uint32_t opcode, std::span<const std::uint32_t> packet);
// Whether a NOP packet is one of our markers (validation accepts sub-op 0 NOPs).
bool IsMarker(std::span<const std::uint32_t> packet);

}

#endif

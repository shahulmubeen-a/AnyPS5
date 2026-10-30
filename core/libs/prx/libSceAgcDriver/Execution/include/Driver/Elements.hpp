#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_ELEMENTS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_ELEMENTS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>

// Porter element costs (APS5_ELEM=1 or APS5_ELEM_FILE=<control file>): every packet a queue worker executes is
// timed from the top of its loop iteration to the end (drains, flushes, memory preparation, waits and the packet
// itself) and charged to the packet's element:
// - its pass: the queue and the title's debug-marker stack (sceAgcDcbPushMarker / PopMarker names);
// - for queue-0 draws inside an engine draw call (porter L3 markers, APS5_PORTER_L3=1): the engine function kind
//   or VA and the material bound by the engine's BindMaterial;
// - the draw's first/last program or the dispatch's program.
// Draw-pipeline commits and preparations carry the element of the packet that enqueued them, so the committer's
// time is charged too. "[elem-*]" lines report the top elements every 10 s.
// APS5_ELEM_FILE is re-read (by modification time, at most every 0.5 s) for skip rules, one per line:
//   pass <text>   skip draws and dispatches whose pass label contains <text> (label: "q<queue>|m1/m2/...")
//   passd <text>  only draws; passc <text> only dispatches; passx <label> only the label itself (exact)
//   prog <hex>    skip draws (first or last program) and dispatches (program) with this program address
//   kind <name>   skip draws inside engine calls of this kind (item, instanced, instanced-b, other)
//   va <hex>      skip draws inside the engine function with this eboot VA (l3-other hooks)
//   mat <hex>     skip draws while this material is bound
//   queue <hex>   skip every draw and dispatch on this queue
//   gkind/gva/gmat ...  the same at the guest: the engine function returns at once, so the title never
//                 writes the packets (porter L3 hooks only)
//   every <n>     pass/passx rules listed after this line skip their pass except on every n-th present
//   deferstores on|off  GPU-ordered storage write-backs (Texture.cpp GpuOrderedReasons; on by default since
//                 2026-10-30) on or off live: the runtime switch APS5_NO_DEFER_STORES
//   lockprio on|off  queue 0's worker and the draw committer take GpuMutex first (APS5_GPU_LOCK_PRIORITY, live)
//   gpoke <va> <u32>  writes a dword into the game's data at an eboot VA while listed (engine knobs, e.g. the
//                 shadow cascade count at 0x213d794); the original value comes back when the line goes
//   switch <APS5_NAME> on|off  sets a runtime switch of the in-run A/B registry (RuntimeSwitches.cpp) live
//   commitdelay <us>  busy-waits <us> after every draw-pipeline commit (causal profiling: frame-time slope)
//   q0delay <us>  busy-waits <us> before every queue-0 draw or dispatch packet (same, for the queue-0 worker)
// '#' starts a comment. Skipped packets are counted per element.
namespace AgcDriver {
struct QueueState;
}

namespace AgcDriver::Elements {

bool Enabled();

// The element of the packet the calling worker thread executes; draw-pipeline items copy it.
struct Tag {
    std::uint32_t label = 0;
    std::uint32_t engineSlot = 0;
    std::uint32_t materialSlot = 0;
    std::uint32_t programSlot = 0;
};
Tag CurrentTag();
// Counts presents (queue 0's flips) for the "every <n>" rules.
void NoteFlip();

class PacketScope {
public:
    PacketScope() = default;
    PacketScope(const PacketScope&) = delete;
    PacketScope& operator=(const PacketScope&) = delete;
    // Starts timing the packet; returns true when the packet is a draw or dispatch that a skip rule removes.
    // `offset`: the packet's dword offset in its submission (frame order of queue-0 passes).
    bool Begin(std::uint32_t queueId, const QueueState& queue, std::uint32_t opcode, bool draw, std::size_t offset);
    ~PacketScope();

private:
    bool active = false;
    bool skipped = false;
    std::uint8_t type = 0;
    std::chrono::steady_clock::time_point start{};
};

// The worker phases of the current packet (APS5_PROFILE_DRAW: DispatchTiming.hpp DriverPhase, DrawTiming.hpp
// DrawDriverPhase), charged to its label and program.
void NotePhases(bool dispatch, const double* ms, std::size_t count);
// The committer's phases of one recorded draw (Graphics Draw.cpp DrawPhase, microseconds), charged to the element
// of the draw-pipeline item being committed (SetCommitTag on the committer thread).
void SetCommitTag(const Tag* tag);
void NoteCommitPhases(const double* us, std::size_t count);
// From Driver::draw after the programs are decoded (queue worker thread).
void NoteDrawPrograms(std::uint64_t first, std::uint64_t last);
bool SkipDrawPrograms(std::uint64_t first, std::uint64_t last);
// Committer / preparer time of an enqueued item.
void NoteCommit(const Tag& tag, std::uint64_t ns, bool prepare);
// The "commitdelay <us>" rule: a busy wait after each draw-pipeline commit (committer thread).
void DelayCommit();
// Guest-level skip rules for the porter L3 hook handlers: kind 1 item, 2 instanced, 3 instanced-b, 10 other.
bool GuestSkip(std::uint32_t kind, std::uint64_t va, std::uint64_t material);

}

#endif

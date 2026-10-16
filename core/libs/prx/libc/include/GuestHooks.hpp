#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GUESTHOOKS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GUESTHOOKS_HPP

#include <cstddef>
#include <cstdint>

// Entry hooks on guest functions (Windows). APS5_GUEST_HOOKS names a table file, one hook per line:
//   <handler> <eboot VA, hex> <expected first bytes, hex, at least 8> [label]
// '#' starts a comment. At libc start-up, before the entry stub runs any guest code, each listed
// function in app.exe (eboot VA at the image base + the .elf0 section RVA) gets a 5-byte jump to a
// stub that saves the argument registers into a GuestHookContext, calls the handler and continues in
// the original function. The bytes at the entry must equal the expected bytes or the hook is refused,
// so a table only ever patches the build it was written for. Handlers run on the guest thread that
// called the function, may read the context and may change argument registers; they must not call
// back into guest code. Built-in handlers: count (hits only, reported every 10 s as "[hooks]"),
// args (the first APS5_GUEST_HOOKS_LOG calls, default 16, as "[hook]" lines) and dump (args plus
// 64 bytes at rdi and at rsi).

struct GuestHookContext {
    std::uint8_t ymm[8][32];
    std::uint64_t r11;
    std::uint64_t r10;
    std::uint64_t rax;
    std::uint64_t r9;
    std::uint64_t r8;
    std::uint64_t rcx;
    std::uint64_t rdx;
    std::uint64_t rsi;
    std::uint64_t rdi;
    std::uint64_t returnAddress;
};

struct GuestHookInfo {
    const char* handler;
    const char* label;
    std::uint64_t va;
    std::uint64_t address;
};

using GuestHookHandler = void (*)(GuestHookContext& context, const GuestHookInfo& info, std::uint64_t hit);

struct GuestHookInstruction {
    std::size_t length;
    // Offset of a RIP-relative disp32 inside the instruction, or -1.
    int ripDisplacement;
};

extern "C" {
// Registers a handler that hook lines can name; later registrations of the same name are refused.
bool RegisterGuestHookHandler_nid_no_patch(const char* name, GuestHookHandler handler);
// Installs an entry hook at a host address (the table loader calls this for every line).
// Returns false and fills `why` (if given) when the hook is refused.
bool InstallGuestHook_nid_no_patch(void* address, const std::uint8_t* expected, std::size_t expectedBytes, const char* handler, const char* label, std::uint64_t va, char* why, std::size_t whyBytes);
std::uint64_t GuestHookHits_nid_no_patch(const char* label);
// The instruction whitelist the patcher relocates (exposed for tests; yotei-work's hookbytes.py mirrors it).
bool DecodeGuestHookInstruction_nid_no_patch(const std::uint8_t* code, std::size_t available, GuestHookInstruction* out);
}

#endif

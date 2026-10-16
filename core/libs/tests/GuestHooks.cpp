#include "prx/libc/include/GuestHooks.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <source_location>
#include <vector>

static void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::fprintf(stderr, "Guest hook check failed at line %u\n", location.line());
        std::abort();
    }
}

static void Decodes(std::initializer_list<std::uint8_t> bytes, std::size_t length, int ripDisplacement, std::source_location location = std::source_location::current()) {
    const std::vector<std::uint8_t> code(bytes);
    GuestHookInstruction instruction{};
    Require(DecodeGuestHookInstruction_nid_no_patch(code.data(), code.size(), &instruction), location);
    Require(instruction.length == length && instruction.ripDisplacement == ripDisplacement, location);
}

static void Refuses(std::initializer_list<std::uint8_t> bytes, std::source_location location = std::source_location::current()) {
    const std::vector<std::uint8_t> code(bytes);
    GuestHookInstruction instruction{};
    Require(!DecodeGuestHookInstruction_nid_no_patch(code.data(), code.size(), &instruction), location);
}

static std::uint64_t g_seenRdi = 0;
static std::uint64_t g_seenRsi = 0;
static std::uint64_t g_seenReturn = 0;
static std::uint64_t g_lateCalls = 0;

static void AddHundred(GuestHookContext& context, const GuestHookInfo& info, std::uint64_t hit) {
    Require(std::strcmp(info.label, "adder") == 0 && hit >= 1);
    g_seenRdi = context.rdi;
    g_seenRsi = context.rsi;
    g_seenReturn = context.returnAddress;
    context.rdi += 100;
}

static void Late(GuestHookContext&, const GuestHookInfo&, std::uint64_t) {
    ++g_lateCalls;
}

using Binary = std::uint64_t (APS5_VABI*)(std::uint64_t, std::uint64_t);
using Nullary = std::uint64_t (APS5_VABI*)();
using Floats = float (APS5_VABI*)(float, float);

static std::uint8_t* Emit(std::uint8_t* at, std::initializer_list<std::uint8_t> bytes) {
    std::memcpy(at, std::data(bytes), bytes.size());
    return at + bytes.size();
}

static bool Install(void* function, std::size_t expectedBytes, const char* handler, const char* label, char* why = nullptr) {
    char ignored[256] = {};
    return InstallGuestHook_nid_no_patch(function, static_cast<const std::uint8_t*>(function), expectedBytes, handler, label, 0, why != nullptr ? why : ignored, 256);
}

int main() {
    // The whitelist (kept in sync with yotei-work/engine/tools/hookbytes.py).
    Decodes({0x55}, 1, -1);
    Decodes({0x41, 0x57}, 2, -1);
    Decodes({0x48, 0x89, 0xe5}, 3, -1);
    Decodes({0x48, 0x83, 0xec, 0x28}, 4, -1);
    Decodes({0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00}, 7, -1);
    Decodes({0x48, 0x8b, 0x05, 1, 2, 3, 4}, 7, 3);
    Decodes({0x8a, 0x05, 1, 2, 3, 4}, 6, 2);
    Decodes({0x80, 0x3d, 1, 2, 3, 4, 0}, 7, 2);
    Decodes({0x4c, 0x8d, 0x3d, 1, 2, 3, 4}, 7, 3);
    Decodes({0xbf, 0x90, 0, 0, 0}, 5, -1);
    Decodes({0x41, 0xb8, 1, 0, 0, 0}, 6, -1);
    Decodes({0x48, 0xb8, 1, 2, 3, 4, 5, 6, 7, 8}, 10, -1);
    Decodes({0x48, 0x8b, 0x47, 0x08}, 4, -1);
    Decodes({0x48, 0x8b, 0x44, 0x24, 0x08}, 5, -1);
    Decodes({0x48, 0x8b, 0x87, 1, 2, 3, 4}, 7, -1);
    Decodes({0xc7, 0x45, 0xfc, 1, 0, 0, 0}, 7, -1);
    Decodes({0x31, 0xc0}, 2, -1);
    Refuses({0xc5, 0xf8, 0x77});
    Refuses({0xe8, 1, 2, 3, 4});
    Refuses({0x48, 0x8d, 0xe5});
    Refuses({0x48, 0x8b, 0x04, 0x25, 1, 2, 3, 4});
    Refuses({0x0f, 0x1f, 0x44, 0x00, 0x00});
    Refuses({0x48});

    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(code != nullptr);

    // rdi + rsi behind a frame-pointer prologue.
    auto* adder = code;
    Emit(adder, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x48, 0x8d, 0x04, 0x37, 0x41, 0x5f, 0x5d, 0xc3, 0xcc, 0xcc});
    // Loads a qword through a RIP-relative mov inside the patched prologue.
    auto* loader = code + 64;
    auto* constant = code + 192;
    const std::uint64_t value = 0x1122334455667788ull;
    std::memcpy(constant, &value, sizeof(value));
    {
        auto* at = Emit(loader, {0x55, 0x48, 0x89, 0xe5, 0x48, 0x8b, 0x05});
        const auto displacement = static_cast<std::int32_t>(constant - (at + 4));
        std::memcpy(at, &displacement, sizeof(displacement));
        Emit(at + 4, {0x5d, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc});
    }
    // xmm0 + xmm1 (checks that the thunk keeps vector arguments).
    auto* floats = code + 128;
    Emit(floats, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0xf3, 0x0f, 0x58, 0xc1, 0x41, 0x5f, 0x5d, 0xc3, 0xcc, 0xcc});
    // A prologue the patcher must refuse.
    auto* vector = code + 256;
    Emit(vector, {0xc5, 0xf8, 0x77, 0x55, 0x48, 0x89, 0xe5, 0x5d, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc});
    FlushInstructionCache(GetCurrentProcess(), code, 4096);

    Require(reinterpret_cast<Binary>(adder)(2, 3) == 5);
    Require(reinterpret_cast<Nullary>(loader)() == value);
    Require(reinterpret_cast<Floats>(floats)(1.5f, 2.25f) == 3.75f);

    Require(RegisterGuestHookHandler_nid_no_patch("test-add-hundred", &AddHundred));
    Require(!RegisterGuestHookHandler_nid_no_patch("test-add-hundred", &Late));
    Require(Install(adder, 16, "test-add-hundred", "adder"));
    Require(Install(loader, 16, "count", "loader"));
    Require(Install(floats, 16, "test-late", "floats"));

    char why[256] = {};
    Require(!Install(vector, 16, "count", "vector", why));
    Require(std::strstr(why, "unsupported instruction at +0") != nullptr);
    const std::uint8_t wrong[8] = {0x55, 0x48, 0x89, 0xe5, 0x00, 0x00, 0x00, 0x00};
    Require(!InstallGuestHook_nid_no_patch(code + 512, wrong, sizeof(wrong), "count", "wrong", 0, why, sizeof(why)));
    Require(std::strstr(why, "bytes differ") != nullptr);

    Require(adder[0] == 0xe9);
    const auto result = reinterpret_cast<Binary>(adder)(2, 3);
    Require(result == 105);
    Require(g_seenRdi == 2 && g_seenRsi == 3 && g_seenReturn != 0);
    Require(GuestHookHits_nid_no_patch("adder") == 1);

    Require(reinterpret_cast<Nullary>(loader)() == value);
    Require(reinterpret_cast<Nullary>(loader)() == value);
    Require(GuestHookHits_nid_no_patch("loader") == 2);

    Require(reinterpret_cast<Floats>(floats)(1.5f, 2.25f) == 3.75f);
    Require(g_lateCalls == 0 && GuestHookHits_nid_no_patch("floats") == 1);
    Require(RegisterGuestHookHandler_nid_no_patch("test-late", &Late));
    Require(reinterpret_cast<Floats>(floats)(0.5f, 0.25f) == 0.75f);
    Require(g_lateCalls == 1 && GuestHookHits_nid_no_patch("floats") == 2);

    std::puts("guest hooks ok");
    return 0;
}

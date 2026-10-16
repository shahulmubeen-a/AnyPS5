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

    // Patches: an immediate in code, and a table on a read-only page (protection restored).
    auto* immediate = code + 320;
    Emit(immediate, {0xb8, 0x00, 0x0f, 0x00, 0x00, 0xc3, 0xcc, 0xcc});
    FlushInstructionCache(GetCurrentProcess(), immediate, 8);
    Require(reinterpret_cast<Nullary>(immediate)() == 0xf00);
    const std::uint8_t wide[] = {0x00, 0x0f, 0x00, 0x00};
    const std::uint8_t narrow[] = {0x80, 0x07, 0x00, 0x00};
    Require(ApplyGuestPatch_nid_no_patch(immediate + 1, wide, narrow, sizeof(wide), why, sizeof(why)));
    Require(reinterpret_cast<Nullary>(immediate)() == 0x780);
    Require(!ApplyGuestPatch_nid_no_patch(immediate + 1, wide, narrow, sizeof(wide), why, sizeof(why)));
    Require(std::strstr(why, "bytes differ") != nullptr && reinterpret_cast<Nullary>(immediate)() == 0x780);
    auto* table = static_cast<std::uint32_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(table != nullptr);
    table[0] = 3840;
    table[1] = 2160;
    DWORD previous = 0;
    Require(VirtualProtect(table, 4096, PAGE_READONLY, &previous));
    const std::uint32_t large[] = {3840, 2160};
    const std::uint32_t small[] = {1920, 1080};
    Require(ApplyGuestPatch_nid_no_patch(table, reinterpret_cast<const std::uint8_t*>(large), reinterpret_cast<const std::uint8_t*>(small), sizeof(large), why, sizeof(why)));
    Require(table[0] == 1920 && table[1] == 1080);
    MEMORY_BASIC_INFORMATION page{};
    Require(VirtualQuery(table, &page, sizeof(page)) == sizeof(page) && page.Protect == PAGE_READONLY);

    // A patch across a read-only page and a data page writes both and leaves each page its protection
    // (no execute bit on data); memory without read access is refused before anything is read or written.
    auto* pages = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 3 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(pages != nullptr);
    std::memset(pages, 0x11, 3 * 4096);
    Require(VirtualProtect(pages, 4096, PAGE_READONLY, &previous));
    Require(VirtualProtect(pages + 2 * 4096, 4096, PAGE_NOACCESS, &previous));
    const std::uint8_t before[8] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    const std::uint8_t after[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Require(ApplyGuestPatch_nid_no_patch(pages + 4096 - 4, before, after, sizeof(after), why, sizeof(why)));
    Require(std::memcmp(pages + 4096 - 4, after, sizeof(after)) == 0);
    Require(VirtualQuery(pages, &page, sizeof(page)) == sizeof(page) && page.Protect == PAGE_READONLY);
    Require(VirtualQuery(pages + 4096, &page, sizeof(page)) == sizeof(page) && page.Protect == PAGE_READWRITE);
    Require(!ApplyGuestPatch_nid_no_patch(pages + 2 * 4096, before, after, sizeof(after), why, sizeof(why)));
    Require(std::strstr(why, "not committed readable memory at +0") != nullptr);
    Require(!ApplyGuestPatch_nid_no_patch(pages + 2 * 4096 - 4, before, after, sizeof(after), why, sizeof(why)));
    Require(std::strstr(why, "not committed readable memory at +4") != nullptr);
    Require(std::memcmp(pages + 2 * 4096 - 4, before, 4) == 0);
    Require(VirtualQuery(pages + 2 * 4096, &page, sizeof(page)) == sizeof(page) && page.Protect == PAGE_NOACCESS);

    // Patch and hook targets in app.exe's guest sections, laid out as the relinker writes Yotei's eboot:
    // code, a gap without a section, read-only data, data.
    const GuestImageSection sections[] = {
        {0x0, 0x17e9000, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, ".elf0"},
        {0x17ec000, 0x4a8000, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ, ".elf2"},
        {0x1c94000, 0x54ea000, IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, ".elf3"},
    };
    constexpr std::size_t sectionCount = sizeof(sections) / sizeof(sections[0]);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0xde020b, 5, false, why, sizeof(why)) == 0);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0xde5ae0, 16, true, why, sizeof(why)) == 0);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x1af8190, 128, false, why, sizeof(why)) == 1);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x213d794, 4, false, why, sizeof(why)) == 2);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x17e9800, 4, false, why, sizeof(why)) == -1);
    Require(std::strstr(why, "outside every guest section") != nullptr);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x17ec000 + 0x4a8000 - 2, 4, false, why, sizeof(why)) == -1);
    Require(std::strstr(why, "crosses the end of section .elf2") != nullptr);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x1af8190, 16, true, why, sizeof(why)) == -1);
    Require(std::strstr(why, "executable section, not .elf2") != nullptr);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x1c94000 + 0x54ea000, 1, false, why, sizeof(why)) == -1);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, ~0ull - 1, 4, false, why, sizeof(why)) == -1);
    Require(FindGuestTargetSection_nid_no_patch(sections, sectionCount, 0x213d794, 0, false, why, sizeof(why)) == -1);

    std::puts("guest hooks ok");
    return 0;
}

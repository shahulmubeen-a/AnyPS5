#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEBUGLABELS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEBUGLABELS_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>

// DEBUG (APS5_DEBUG_LABELS=1, with APS5_ELEM=1 or APS5_ELEM_FILE): the title's pass names reach the Vulkan command
// stream as VK_EXT_debug_utils labels, so GPU tools (Nsight Graphics GPU Trace and Graphics Capture, RenderDoc) show
// "q0|Frame/Deferred" style ranges instead of an unnamed list of draws and dispatches.
// - The name is the porter element label of the packet being recorded (Elements.hpp: queue and the title's
//   sceAgcDcbPushMarker stack), read through the provider Elements.cpp installs.
// - A label begins before the first draw or dispatch of a pass in a command buffer and ends before the first one of
//   another pass or at the command buffer's end, so every command buffer is balanced on its own. Copies, barriers
//   and uploads recorded between two draws fall into the label that is open.
// - Without the switch nothing is looked up or recorded. The instance enables VK_EXT_debug_utils only with the
//   switch (VulkanDevice.cpp); when the device has no label entry points the feature turns itself off.
namespace AgcDriver::Graphics::DebugLabels {

struct Provider {
    // The label slot of the packet the calling thread records, ~0u for none.
    std::uint32_t (*current)() = nullptr;
    std::string (*text)(std::uint32_t slot) = nullptr;
};

[[nodiscard]] inline bool Enabled() noexcept {
    static const bool enabled = std::getenv("APS5_DEBUG_LABELS") != nullptr;
    return enabled;
}

// APS5_DEBUG_LABELS=2 also labels the driver's own command classes (Recorder::CommandClass: staging copies, storage
// uploads and write-backs, shadow publishes, fills, copies, ...) as "aps5:<class>" ranges, so a GPU tool attributes
// their dispatches, stalls and bus traffic. A class range is flat: it ends the open pass label, and the next draw or
// dispatch begins its pass label again.
[[nodiscard]] inline int Level() noexcept {
    static const int level = [] {
        const char* value = std::getenv("APS5_DEBUG_LABELS");
        return value == nullptr ? 0 : std::max(1, std::atoi(value));
    }();
    return level;
}

namespace Detail {
struct State {
    std::mutex mutex;
    Provider provider;
    // Open label per command buffer being recorded (absent: none open).
    std::unordered_map<VkCommandBuffer, std::uint32_t> open;
    // Nesting depth of class ranges per command buffer (only the outermost is a label).
    std::unordered_map<VkCommandBuffer, std::uint32_t> classes;
    std::unordered_map<std::uint32_t, std::string> texts;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkCmdBeginDebugUtilsLabelEXT begin = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT end = nullptr;
    bool unavailable = false;
    std::uint64_t begun = 0;
};
inline State& Get() {
    static auto* state = new State();
    return *state;
}
inline std::atomic<bool>& Armed() {
    static std::atomic<bool> armed{false};
    return armed;
}
// Called with the state's mutex held.
inline bool Resolve(State& state, const Context& context) {
    if (state.unavailable || context.deviceProc == nullptr) return false;
    if (state.device == context.device && state.begin != nullptr) return true;
    state.begin = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(context.deviceProc(context.device, "vkCmdBeginDebugUtilsLabelEXT"));
    state.end = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(context.deviceProc(context.device, "vkCmdEndDebugUtilsLabelEXT"));
    state.device = context.device;
    if (state.begin == nullptr || state.end == nullptr) {
        state.unavailable = true;
        std::fprintf(stderr, "[debug-labels] off: the device has no vkCmdBeginDebugUtilsLabelEXT (VK_EXT_debug_utils is not enabled on the instance)\n");
        return false;
    }
    std::fprintf(stderr, "[debug-labels] on: pass names are recorded as VK_EXT_debug_utils labels (APS5_DEBUG_LABELS)\n");
    return true;
}
}

// Elements.cpp, once, when both switches are set.
inline void SetProvider(Provider provider) {
    auto& state = Detail::Get();
    std::lock_guard lock(state.mutex);
    state.provider = provider;
    Detail::Armed().store(provider.current != nullptr && provider.text != nullptr, std::memory_order_release);
}

// After vkBeginCommandBuffer: a reused command buffer starts without an open label.
inline void Begun(VkCommandBuffer commands) {
    if (!Detail::Armed().load(std::memory_order_acquire)) return;
    auto& state = Detail::Get();
    std::lock_guard lock(state.mutex);
    state.open.erase(commands);
    state.classes.erase(commands);
}

// Before a draw or dispatch is recorded into `commands`.
inline void Mark(const Context& context, VkCommandBuffer commands) {
    if (!Detail::Armed().load(std::memory_order_acquire)) return;
    auto& state = Detail::Get();
    std::lock_guard lock(state.mutex);
    if (state.classes.contains(commands)) return;
    const auto slot = state.provider.current();
    if (slot == ~0u) return;
    const auto found = state.open.find(commands);
    if (found != state.open.end() && found->second == slot) return;
    if (!Detail::Resolve(state, context)) return;
    if (found != state.open.end()) state.end(commands);
    auto text = state.texts.find(slot);
    if (text == state.texts.end()) text = state.texts.emplace(slot, state.provider.text(slot)).first;
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = text->second.c_str();
    state.begin(commands, &label);
    state.open[commands] = slot;
    ++state.begun;
}

// Before vkEndCommandBuffer.
inline void Close(VkCommandBuffer commands) {
    if (!Detail::Armed().load(std::memory_order_acquire)) return;
    auto& state = Detail::Get();
    std::lock_guard lock(state.mutex);
    if (state.classes.erase(commands) != 0 && state.end != nullptr) state.end(commands);
    const auto found = state.open.find(commands);
    if (found == state.open.end()) return;
    if (state.end != nullptr) state.end(commands);
    state.open.erase(found);
}

namespace Detail {
inline std::vector<VkCommandBuffer>& ClassStack() {
    thread_local std::vector<VkCommandBuffer> stack;
    return stack;
}
}

// Recorder::BeginGpuTiming for a command class (APS5_DEBUG_LABELS=2): true when EndClass is owed.
inline bool BeginClass(const Context& context, VkCommandBuffer commands, const char* name) {
    if (!Detail::Armed().load(std::memory_order_acquire) || commands == VK_NULL_HANDLE) return false;
    auto& state = Detail::Get();
    std::lock_guard lock(state.mutex);
    if (!Detail::Resolve(state, context)) return false;
    auto& depth = state.classes[commands];
    if (depth++ == 0) {
        if (const auto found = state.open.find(commands); found != state.open.end()) {
            state.end(commands);
            state.open.erase(found);
        }
        thread_local std::string text;
        text.assign("aps5:").append(name);
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = text.c_str();
        state.begin(commands, &label);
    }
    Detail::ClassStack().push_back(commands);
    return true;
}

inline void EndClass() {
    auto& stack = Detail::ClassStack();
    if (stack.empty()) return;
    const auto commands = stack.back();
    stack.pop_back();
    auto& state = Detail::Get();
    std::lock_guard lock(state.mutex);
    const auto found = state.classes.find(commands);
    if (found == state.classes.end()) return;
    if (--found->second != 0) return;
    state.classes.erase(found);
    if (state.end != nullptr) state.end(commands);
}

}

#endif

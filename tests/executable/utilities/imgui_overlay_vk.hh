/* Copyright 2026 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#ifndef WBE_FILE_IMGUI_OVERLAY_VK_HH
#define WBE_FILE_IMGUI_OVERLAY_VK_HH

#include "SDL_events.h"
#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "function/render_vk/renderer_vk.hh"
#include <mutex>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class ImGuiOverlayVK
 * @brief Executable-test Dear ImGui overlay drawn by the Vulkan renderer.
 *
 * The overlay owns the Dear ImGui context and its Vulkan backend. The backend allocates its descriptor sets from the
 * DescriptorSetPluginVK pool, so the renderer must be created with RendererCapabilities::descriptor_set. The overlay
 * receives window input by watching SDL events: the watch runs on the thread that pumps window events and only queues them;
 * begin_frame() feeds them to Dear ImGui on the rendering thread.
 *
 * Each frame, the rendering thread calls begin_frame(), issues Dear ImGui calls, and calls end_frame() before the renderer
 * ticks; an RTImGuiVK task in the active render graph then records the frame with record(). Construct the overlay before the
 * rendering thread starts or on it, and destroy it after the rendering thread has stopped and before the renderer is
 * destroyed. The overlay must outlive every recording of the tasks that use it. At most one overlay exists at a time.
 *
 * Neither copyable nor movable.
 */
class ImGuiOverlayVK {
public:
    ImGuiOverlayVK() = delete;

    /**
     * @brief Create the Dear ImGui context and its Vulkan backend for the swap chain.
     *
     * @param p_renderer The renderer whose swap chain the overlay is drawn into; it must outlive the overlay.
     * @throws std::runtime_error If the descriptor-set plugin is not enabled, another overlay exists, or the backend fails to
     * initialize.
     */
    explicit ImGuiOverlayVK(RendererVK* p_renderer);

    ~ImGuiOverlayVK();
    ImGuiOverlayVK(const ImGuiOverlayVK&) = delete;
    ImGuiOverlayVK(ImGuiOverlayVK&&) noexcept = delete;
    ImGuiOverlayVK& operator=(const ImGuiOverlayVK&) = delete;
    ImGuiOverlayVK& operator=(ImGuiOverlayVK&&) noexcept = delete;

    /**
     * @brief Start a Dear ImGui frame after feeding it the window input queued since the previous frame.
     *
     * Must be called on the rendering thread.
     *
     * @param p_delta The time since the previous frame in seconds.
     */
    void begin_frame(double p_delta);

    /**
     * @brief Finish the Dear ImGui frame and upload the textures it changed.
     *
     * Must be called on the rendering thread, after begin_frame() and before the renderer records the frame.
     */
    void end_frame();

    /**
     * @brief Record the draw commands of the last finished frame.
     *
     * Must be called on the rendering thread inside a rendering scope whose single color attachment has the swap-chain
     * format. Records nothing when no frame has been finished.
     *
     * @param p_command_buffer The command buffer in the recording state.
     */
    void record(VkCommandBuffer p_command_buffer);

private:
    const RenderContextVK* render_data = nullptr;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::mutex event_mutex;
    Vector<SDL_Event, HeapAllocatorGlobal> pending_events;
    Vector<SDL_Event, HeapAllocatorGlobal> frame_events;

    static int on_sdl_event(void* p_user_data, SDL_Event* p_event);
    void process_event(const SDL_Event& p_event);
};

} // namespace WhiteBirdEngine

#endif

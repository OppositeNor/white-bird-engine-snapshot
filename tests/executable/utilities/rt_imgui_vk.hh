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
#ifndef WBE_FILE_RT_IMGUI_VK_HH
#define WBE_FILE_RT_IMGUI_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "imgui_overlay_vk.hh"
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTImGuiVK
 * @brief Executable-test render task that draws the frame of an ImGuiOverlayVK over its color target.
 *
 * The task loads `render_target_colors[0]` and renders the overlay's last finished frame into it with the swap-chain
 * extent, so map the color target to an image with the swap-chain format and extent, usually REND_RES_SWAP_CHAIN, through
 * RendTaskDescVK::alias(), and connect the task after the previous writer of that image.
 *
 * Neither copyable nor movable. Confined to the rendering thread.
 */
struct RTImGuiVK final : public IRenderTaskVK {
    RTImGuiVK() = delete;
    virtual ~RTImGuiVK() override = default;
    RTImGuiVK(const RTImGuiVK&) = delete;
    RTImGuiVK(RTImGuiVK&&) noexcept = delete;
    RTImGuiVK& operator=(const RTImGuiVK&) = delete;
    RTImGuiVK& operator=(RTImGuiVK&&) noexcept = delete;

    /**
     * @brief Construct the task.
     *
     * @param p_render_data Renderer data that outlives the task.
     * @param p_overlay The overlay to draw; it must outlive every recording of the task.
     * @throws std::runtime_error If p_overlay is null.
     */
    RTImGuiVK(const RenderContextVK* p_render_data, ImGuiOverlayVK* p_overlay);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void bind_resources(const RendResViewVK& p_resources) override;
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    ImGuiOverlayVK* overlay = nullptr;
    RenderTargetsVK render_targets;
};

} // namespace WhiteBirdEngine

#endif

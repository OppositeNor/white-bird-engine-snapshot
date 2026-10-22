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
#include "rt_imgui_vk.hh"
#include "core/allocator/stl_allocator.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_targets_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "imgui_overlay_vk.hh"
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

ImGuiOverlayVK* require_overlay(ImGuiOverlayVK* p_overlay) {
    if (p_overlay == nullptr) {
        throw std::runtime_error("RTImGuiVK: overlay is null.");
    }
    return p_overlay;
}

} // namespace

RTImGuiVK::RTImGuiVK(const RenderContextVK* p_render_data, ImGuiOverlayVK* p_overlay)
    : render_data(p_render_data), overlay(require_overlay(p_overlay)),
      render_targets(p_render_data->allocator, 1, false, RenderTargetsVK::Config{.color_load_op = VK_ATTACHMENT_LOAD_OP_LOAD}) {
}

void RTImGuiVK::bind_resources(const RendResViewVK& p_resources) {
    render_targets.bind_resources(p_resources);
}

void RTImGuiVK::append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    render_targets.append_accesses(p_reads, p_writes);
}

void RTImGuiVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    render_targets.begin_rendering(render_data, p_command_buffer, render_data->swap_chain_extent);
    overlay->record(p_command_buffer);
    vkCmdEndRendering(p_command_buffer);
}

} // namespace WhiteBirdEngine

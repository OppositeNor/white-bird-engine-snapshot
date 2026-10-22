/* Copyright 2025 OppositeNor

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
#include "function/render_vk/rt_full_memory_barrier_vk.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_params_full_memory_barrier_vk.hh"
#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTFullMemoryBarrierVK::RTFullMemoryBarrierVK(std::string_view p_resource_id) : resource_id(p_resource_id) {
    if (resource_id.empty()) {
        throw std::runtime_error("RTFullMemoryBarrierVK: resource id is empty.");
    }
}

Unique<IRenderTaskVK> RTFullMemoryBarrierVK::create(
    const RenderContextVK* p_render_data, const RTParamsFullMemoryBarrierVK& p_params, const RendGraphContextVK& /*p_context*/) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("RTFullMemoryBarrierVK: render data is unavailable.");
    }
    // Rejected before the task is allocated.
    if (p_params.resource.empty()) {
        throw std::runtime_error("RTFullMemoryBarrierVK: resource id is empty.");
    }
    return make_unique<RTFullMemoryBarrierVK>(p_render_data->allocator, p_params.resource);
}

void RTFullMemoryBarrierVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& /*p_reads*/, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    // The barrier covers every command, which the edge to the next task synchronizes after.
    p_writes.push_back({.res_id = resource_id, .usage = RendResUsageVK::ALL_COMMANDS});
}

void RTFullMemoryBarrierVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
    };
    VkDependencyInfo dependency{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(p_command_buffer, &dependency);
}

} // namespace WhiteBirdEngine

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
#include "function/render_vk/rt_build_acceleration_structures_vk.hh"

#include "core/allocator/stl_allocator.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_context_vk.hh"

#include <stdexcept>
#include <string_view>
#include <utility>

#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RTBuildAccelerationStructuresVK::RTBuildAccelerationStructuresVK(
    const RenderContextVK* p_render_data, std::string_view p_acceleration_structure_resource_id, GPUJobVK p_record_job)
    : render_data(p_render_data), acceleration_structure_resource_id(p_acceleration_structure_resource_id),
      record_job(std::move(p_record_job)) {
    if (render_data == nullptr) {
        throw std::runtime_error("RTBuildAccelerationStructuresVK: render data is null.");
    }
    if (acceleration_structure_resource_id.empty()) {
        throw std::runtime_error("RTBuildAccelerationStructuresVK: acceleration-structure resource id is empty.");
    }
    if (!record_job) {
        throw std::runtime_error("RTBuildAccelerationStructuresVK: build callback is empty.");
    }
}

void RTBuildAccelerationStructuresVK::append_accesses(
    Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    static_cast<void>(p_reads);
    p_writes.push_back({.res_id = acceleration_structure_resource_id, .usage = RendResUsageVK::ACCELERATION_STRUCTURE});
}

void RTBuildAccelerationStructuresVK::record(VkCommandBuffer p_command_buffer, double /*p_delta*/) {
    record_job(p_command_buffer);
}

} // namespace WhiteBirdEngine

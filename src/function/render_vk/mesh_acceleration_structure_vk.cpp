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
#include "function/render_vk/mesh_acceleration_structure_vk.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/ro_acceleration_structure_vk.hh"
#include "utils/defs.hh"
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

VkAccelerationStructureBuildGeometryInfoKHR make_build_info(
    VkAccelerationStructureTypeKHR p_type, const VkAccelerationStructureGeometryKHR* p_geometry) {
    VkAccelerationStructureBuildGeometryInfoKHR result{};
    result.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    result.type = p_type;
    result.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    result.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    result.geometryCount = 1;
    result.pGeometries = p_geometry;
    return result;
}

void record_build_barrier(VkCommandBuffer p_command_buffer, VkPipelineStageFlags2 p_dst_stage, VkAccessFlags2 p_dst_access) {
    VkMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstStageMask = p_dst_stage,
        .dstAccessMask = p_dst_access,
    };
    VkDependencyInfo dependency_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
}

} // namespace

MeshAccelerationStructureVK::MeshAccelerationStructureVK(RenderAllocator* p_allocator)
    : allocator(p_allocator), bottom_level_builds(p_allocator), bottom_levels(p_allocator) {
    if (allocator == nullptr) {
        throw std::runtime_error("Failed to construct static-mesh acceleration structure: allocator is null.");
    }
}

Ref<ROAccelerationStructureVK> MeshAccelerationStructureVK::acquire_bottom_level(size_t p_index) const {
    if (p_index >= bottom_levels.size()) {
        throw std::runtime_error("Failed to get static-mesh bottom-level acceleration structure: index is out of bounds.");
    }
    return bottom_levels[p_index];
}

void MeshAccelerationStructureVK::record_build(VkCommandBuffer p_command_buffer) const {
    WBE_DEBUG_ASSERT(command_build_acceleration_structures != nullptr && top_level != MEM_NULL);
    for (size_t i = 0; i < bottom_level_builds.size(); ++i) {
        const BottomLevelBuild& build = bottom_level_builds[i];
        VkAccelerationStructureBuildGeometryInfoKHR build_info =
            make_build_info(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &build.geometry);
        build_info.dstAccelerationStructure = build.acceleration_structure->get_acceleration_structure();
        build_info.scratchData.deviceAddress = bottom_level_scratch_address;
        const VkAccelerationStructureBuildRangeInfoKHR* build_range = &build.build_range;
        command_build_acceleration_structures(p_command_buffer, 1, &build_info, &build_range);
        if (i + 1 < bottom_level_builds.size()) {
            // The next bottom-level build reuses the same scratch storage.
            record_build_barrier(p_command_buffer,
                VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
        }
    }
    // Every bottom-level structure must be complete before the top level references it.
    record_build_barrier(
        p_command_buffer, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);

    VkAccelerationStructureBuildGeometryInfoKHR top_level_build_info =
        make_build_info(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &top_level_geometry);
    top_level_build_info.dstAccelerationStructure = top_level->get_acceleration_structure();
    top_level_build_info.scratchData.deviceAddress = top_level_scratch_address;
    const VkAccelerationStructureBuildRangeInfoKHR* top_level_range = &top_level_build_range;
    command_build_acceleration_structures(p_command_buffer, 1, &top_level_build_info, &top_level_range);

    // Expose the completed top level to later ray-query and ray-tracing shaders.
    record_build_barrier(p_command_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
}

void MeshAccelerationStructureVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    if (top_level == MEM_NULL) {
        throw std::runtime_error("Failed to bind static-mesh acceleration structure: top-level structure is missing.");
    }
    top_level->bind(p_graph, make_member_resource_id(p_resource_id, TOP_LEVEL));
}

} // namespace WhiteBirdEngine

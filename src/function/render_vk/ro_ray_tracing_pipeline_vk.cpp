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
#include "function/render_vk/ro_ray_tracing_pipeline_vk.hh"
#include "function/render/render_database.hh"
#include <format>
#include "function/render_vk/debug_utils_vk.hh"
#include "function/function_utils.hh"
#include "function/render_vk/i_ro_ray_tracing_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RORayTracingPipelineVK::RORayTracingPipelineVK(
    const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data, const Builder& p_builder)
    : IRORayTracingVK(p_render_data, p_ray_tracing_data), shaders(p_render_data->allocator) {
    VkRayTracingPipelineCreateInfoKHR pipeline_create_info{
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
    };
    // Suggested implementation order:
    // 1. Copy builder shader refs into this->shaders so the modules outlive pipeline creation.
    // 2. Materialize VkPipelineShaderStageCreateInfo entries from Builder::shader_stages.
    // 3. Materialize VkRayTracingShaderGroupCreateInfoKHR entries from Builder::shader_groups.
    // 4. Fill pipeline_create_info with stageCount / pStages / groupCount / pGroups.
    // 5. Add recursion depth, descriptor-heap create flags, and optional library/interface pNext structs.
    // 6. Call vkCreateRayTracingPipelinesKHR, store the VkPipeline, then cache whatever SBT layout data create_sbt() needs.

    for (const auto& shader_stage : p_builder.shader_stages) {
        shaders.emplace_back(shader_stage.shader);
    }
    (void)pipeline_create_info;
    (void)p_builder;
    create_sbt();
}

RORayTracingPipelineVK::~RORayTracingPipelineVK() {
    vkDestroyPipeline(render_data->logic_device, pipeline, render_data->allocation_callbacks);
}

TypeID RORayTracingPipelineVK::get_type_uuid() const {
    return TypeUUID<RORayTracingPipelineVK>::UUID;
}

size_t RORayTracingPipelineVK::calculate_sbt_buffer_size(VkPipeline p_ray_pipeline,
    VkRayTracingPipelineCreateInfoKHR p_ray_pipeline_info,
    std::span<const VkRayTracingPipelineCreateInfoKHR> p_libraries_info) {
    WBE_DEBUG_ASSERT(p_ray_pipeline != VK_NULL_HANDLE);

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_properties{};
    ray_tracing_pipeline_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 physical_device_properties{};
    physical_device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physical_device_properties.pNext = &ray_tracing_pipeline_properties;
    vkGetPhysicalDeviceProperties2(render_data->physical_device, &physical_device_properties);

    size_t total_group_count = p_ray_pipeline_info.groupCount;
    for (const VkRayTracingPipelineCreateInfoKHR& library_info : p_libraries_info) {
        if (total_group_count > std::numeric_limits<size_t>::max() - library_info.groupCount) {
            throw std::runtime_error("Shader binding table group count overflow.");
        }
        total_group_count += library_info.groupCount;
    }

    const size_t shader_group_handle_stride = get_align_size(
        ray_tracing_pipeline_properties.shaderGroupHandleSize, ray_tracing_pipeline_properties.shaderGroupHandleAlignment);
    if (total_group_count > std::numeric_limits<size_t>::max() / shader_group_handle_stride) {
        throw std::runtime_error("Shader binding table size overflow.");
    }

    const size_t shader_group_handles_size = total_group_count * shader_group_handle_stride;
    return get_align_size(shader_group_handles_size, ray_tracing_pipeline_properties.shaderGroupBaseAlignment);
}

void RORayTracingPipelineVK::create_sbt() {
    VkRayTracingPipelineCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
    };
    (void)create_info;
    // Suggested implementation order:
    // 1. Query VkPhysicalDeviceRayTracingPipelinePropertiesKHR for handle size and alignment requirements.
    // 2. Reconstruct the final group ordering used for pipeline creation, including linked libraries if you support them.
    // 3. Count records per Builder::SBTRegion so you know the raygen / miss / hit / callable spans.
    // 4. Call calculate_sbt_buffer_size(...) with the created pipeline and the same create-info data used at creation time.
    // 5. Allocate a host-visible staging buffer and a device-local SBT buffer with SHADER_BINDING_TABLE and SHADER_DEVICE_ADDRESS usage.
    // 6. Fetch raw group handles with vkGetRayTracingShaderGroupHandlesKHR.
    // 7. Pack each handle into its aligned SBT record slot, preserving the per-region order expected by vkCmdTraceRaysKHR.
    // 8. Upload the packed records into the device-local SBT buffer and cache strided device-address regions for dispatch.
    auto builder = ROBufferVK::Builder();
    shader_binding_table_buffer =
        builder.set_usage(VK_BUFFER_USAGE_2_SHADER_BINDING_TABLE_BIT_KHR)
            .set_domain(DeviceDomain::DEVICE)
            .set_size(1024 /* Temporary placeholder: replace with calculate_sbt_buffer_size(...) once record counts are wired. */)
            .create_ref(render_data);
}

void RORayTracingPipelineVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_PIPELINE, pipeline, p_name);
    if (shader_binding_table_buffer != MEM_NULL && render_data->render_database != nullptr) {
        render_data->render_database->register_compiled_render_resource(
            std::format("{}.shader_binding_table", p_name), shader_binding_table_buffer);
    }
}

} // namespace WhiteBirdEngine

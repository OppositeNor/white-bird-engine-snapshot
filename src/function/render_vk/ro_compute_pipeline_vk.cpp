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
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include "core/core_utils.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_pipeline_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/utils.hh"
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

Ref<ROComputePipelineVK> ComputePipelineDescVK::create_ref(const RenderContextVK* p_render_context) const {
    WBE_DEBUG_ASSERT(p_render_context != nullptr);
    return p_render_context->create_compute_pipeline(*this);
}

Unique<ROComputePipelineVK> ComputePipelineDescVK::create_unique(const RenderContextVK* p_render_context) const {
    WBE_DEBUG_ASSERT(p_render_context != nullptr);
    return make_unique<ROComputePipelineVK>(p_render_context->allocator, p_render_context, *this);
}

TypeID ROComputePipelineVK::get_type_uuid() const {
    return TypeUUID<ROComputePipelineVK>::UUID;
}

ROComputePipelineVK::~ROComputePipelineVK() = default;

ROComputePipelineVK::ROComputePipelineVK(const RenderContextVK* p_render_data, const Builder& p_builder)
    : ROPipelineVK(p_render_data) {
    const auto& shader_stage = WBE_REQUIRED(p_builder.shader_stage);
    VkPipelineShaderStageCreateInfo shader_stage_create_info{};
    shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shader_stage_create_info.module = shader_stage.shader->get_shader_module();
    shader_stage_create_info.pName = shader_stage.entry_function.c_str();
    VkComputePipelineCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    VkPipelineCreateFlags2CreateInfo create_flags{};
    create_flags.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
    create_flags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    create_info.pNext = &create_flags;
    create_info.stage = shader_stage_create_info;
    create_info.layout = VK_NULL_HANDLE;
    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(
            render_data->logic_device, VK_NULL_HANDLE, 1, &create_info, render_data->allocation_callbacks, &compute_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline.");
    }
    set_pipeline(compute_pipeline);
    exposed_fields.compute_shader = shader_stage.shader;
}

void ROComputePipelineVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_PIPELINE, get_pipeline(), p_name);
}

} // namespace WhiteBirdEngine

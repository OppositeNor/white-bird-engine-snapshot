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
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_pipeline_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/utils.hh"
#include <format>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

Ref<ROGraphicsPipelineVK> GraphicsPipelineDescVK::create_ref(const RenderContextVK* p_render_context) const {
    WBE_DEBUG_ASSERT(p_render_context != nullptr);
    return p_render_context->create_graphics_pipeline(*this);
}

Unique<ROGraphicsPipelineVK> GraphicsPipelineDescVK::create_unique(const RenderContextVK* p_render_context) const {
    WBE_DEBUG_ASSERT(p_render_context != nullptr);
    return make_unique<ROGraphicsPipelineVK>(p_render_context->allocator, p_render_context, *this);
}

TypeID ROGraphicsPipelineVK::get_type_uuid() const {
    return TypeUUID<ROGraphicsPipelineVK>::UUID;
}

ROGraphicsPipelineVK::ROGraphicsPipelineVK(const RenderContextVK* p_render_data, const Builder& p_builder)
    : ROPipelineVK(p_render_data) {
    if (p_render_data == nullptr) {
        throw std::runtime_error("Render data is nullptr.");
    }
    init(p_builder.config);
}

ROGraphicsPipelineVK::~ROGraphicsPipelineVK() = default;

VkPipelineShaderStageCreateInfo ROGraphicsPipelineVK::get_shader_stage_create_info(
    VkShaderStageFlagBits p_stage_flag, const Config::ShaderConfig::ShaderStage& p_stage) {
    VkPipelineShaderStageCreateInfo shader_stage_create_info{};
    shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_create_info.stage = p_stage_flag;
    shader_stage_create_info.module = p_stage.shader->get_shader_module();
    shader_stage_create_info.pName = p_stage.entry_function.c_str();
    return shader_stage_create_info;
}

std::vector<VkPipelineShaderStageCreateInfo> ROGraphicsPipelineVK::gather_shader_stages(const Config& p_config) {
    const auto& vertex_stage = WBE_REQUIRED(p_config.shader.vertex_shader);
    const bool has_geometry_stage = p_config.shader.geometry_shader.has_value();
    const auto& fragment_stage = WBE_REQUIRED(p_config.shader.fragment_shader);

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    shader_stages.reserve(5); // We can have a maximum of 5 shader stages
                              // (vertex, TSC, TSE, geometry, fragment)
    shader_stages.push_back(get_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertex_stage));
    if (has_geometry_stage) {
        shader_stages.push_back(
            get_shader_stage_create_info(VK_SHADER_STAGE_GEOMETRY_BIT, WBE_REQUIRED(p_config.shader.geometry_shader)));
    }
    shader_stages.push_back(get_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_stage));
    // TODO: TSC and TSE shaders
    return shader_stages;
}

void ROGraphicsPipelineVK::cache_exposed_shader_fields(const Config& p_config) {
    const auto& vertex_stage = WBE_REQUIRED(p_config.shader.vertex_shader);
    const bool has_geometry_stage = p_config.shader.geometry_shader.has_value();
    const auto& fragment_stage = WBE_REQUIRED(p_config.shader.fragment_shader);

    exposed_fields.vertex_shader = vertex_stage.shader;
    exposed_fields.geometry_shader = has_geometry_stage ? WBE_REQUIRED(p_config.shader.geometry_shader).shader : MEM_NULL;
    exposed_fields.fragment_shader = fragment_stage.shader;
}

ROGraphicsPipelineVK::PipelineStateCreateInfo ROGraphicsPipelineVK::make_pipeline_state_create_info(const Config& p_config) {
    PipelineStateCreateInfo state_create_info;
    setup_vertex_input_state_create_info(p_config, state_create_info);
    setup_input_assembly_state_create_info(p_config, state_create_info);
    setup_tessellation_state_create_info(p_config, state_create_info);
    setup_viewport_state_create_info(p_config, state_create_info);
    setup_rasterization_state_create_info(p_config, state_create_info);
    setup_mulitsample_state_create_info(p_config, state_create_info);
    setup_depth_stencil_state_create_info(p_config, state_create_info);
    setup_color_blend_state_create_info(p_config, state_create_info);
    setup_dynamic_state_create_info(p_config, state_create_info);
    setup_pipeline_rendering_create_info(p_config, state_create_info);
    return state_create_info;
}

VkGraphicsPipelineCreateInfo ROGraphicsPipelineVK::make_graphics_pipeline_create_info(const Config& p_config,
    const std::vector<VkPipelineShaderStageCreateInfo>& p_shader_stages,
    PipelineStateCreateInfo& p_state_create_info,
    VkPipelineCreateFlags2CreateInfo& p_create_flags) const {
    VkGraphicsPipelineCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    p_create_flags.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
    p_create_flags.pNext = &(p_state_create_info.pipeline_rendering);
    p_create_flags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    create_info.pNext = &p_create_flags;
    create_info.stageCount = p_shader_stages.size();
    create_info.pStages = p_shader_stages.data();
    create_info.pVertexInputState = &(p_state_create_info.vertex_input_state);
    create_info.pInputAssemblyState = &(p_state_create_info.input_assembly_state);
    if (p_state_create_info.tessellation_state.has_value()) {
        create_info.pTessellationState = &(p_state_create_info.tessellation_state.value());
    }
    create_info.pViewportState = &(p_state_create_info.viewport_state);
    if (p_state_create_info.conservative_rasterization_state.sType == VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT) {
        p_state_create_info.rasterization_state.pNext = &p_state_create_info.conservative_rasterization_state;
    }
    create_info.pRasterizationState = &(p_state_create_info.rasterization_state);
    create_info.pMultisampleState = &(p_state_create_info.multisample_state);
    create_info.pDepthStencilState = &(p_state_create_info.depth_stencil_state);
    create_info.pColorBlendState = &(p_state_create_info.color_blend_state);
    create_info.pDynamicState = &(p_state_create_info.dynamic_state);
    create_info.layout = VK_NULL_HANDLE;
    // Using dynamic rendering.
    create_info.renderPass = VK_NULL_HANDLE;
    create_info.subpass = p_config.subpass_index;
    return create_info;
}

VkPipeline ROGraphicsPipelineVK::create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& p_create_info) const {
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    auto create_result = vkCreateGraphicsPipelines(
        render_data->logic_device, VK_NULL_HANDLE, 1, &p_create_info, render_data->allocation_callbacks, &graphics_pipeline);
    if (create_result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create graphics pipeline: {}.", string_VkResult(create_result)));
    }
    return graphics_pipeline;
}

void ROGraphicsPipelineVK::init(const Config& p_config) {
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages = gather_shader_stages(p_config);
    cache_exposed_shader_fields(p_config);
    PipelineStateCreateInfo state_create_info = make_pipeline_state_create_info(p_config);
    VkPipelineCreateFlags2CreateInfo create_flags{};
    VkGraphicsPipelineCreateInfo create_info =
        make_graphics_pipeline_create_info(p_config, shader_stages, state_create_info, create_flags);
    set_pipeline(create_graphics_pipeline(create_info));
    config = p_config;
}

void ROGraphicsPipelineVK::setup_viewport_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& viewport_config = p_config.viewport;
    VkPipelineViewportStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    // Setup viewports.
    create_info.viewportCount = viewport_config.viewports.size();
    create_info.pViewports = viewport_config.viewports.data();
    // Setup scissors.
    create_info.scissorCount = viewport_config.scissors.size();
    create_info.pScissors = viewport_config.scissors.data();
    p_pipeline_create_infos.viewport_state = create_info;
}

void ROGraphicsPipelineVK::setup_vertex_input_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& vertex_input_config = p_config.vertex_input;
    VkPipelineVertexInputStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // Setup bindings.
    create_info.vertexBindingDescriptionCount = vertex_input_config.bindings.size();
    create_info.pVertexBindingDescriptions = vertex_input_config.bindings.data();
    // Setup attributes.
    create_info.vertexAttributeDescriptionCount = vertex_input_config.attributes.size();
    create_info.pVertexAttributeDescriptions = vertex_input_config.attributes.data();
    p_pipeline_create_infos.vertex_input_state = create_info;
}

void ROGraphicsPipelineVK::setup_input_assembly_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& input_assembly_config = p_config.input_assembly;
    VkPipelineInputAssemblyStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    create_info.topology = input_assembly_config.primitive_topology;
    create_info.primitiveRestartEnable = input_assembly_config.primitive_restart_enabled;
    p_pipeline_create_infos.input_assembly_state = create_info;
}

void ROGraphicsPipelineVK::setup_tessellation_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    // TODO
}

void ROGraphicsPipelineVK::setup_rasterization_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& rasterization_config = p_config.rasterization;
    if (rasterization_config.conservative_rasterization && !render_data->renderer_capabilities.conservative_rasterization) {
        throw std::runtime_error("ROGraphicsPipelineVK: conservative rasterization was requested but is unavailable.");
    }
    VkPipelineRasterizationStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    create_info.depthClampEnable = rasterization_config.depth_clamp_enabled;
    create_info.rasterizerDiscardEnable = VK_FALSE;
    create_info.polygonMode = rasterization_config.polygon_mode;
    create_info.cullMode = rasterization_config.cull_mode;
    create_info.frontFace = rasterization_config.front_face;
    create_info.depthBiasEnable = VK_FALSE;
    create_info.lineWidth = 1.0F;
    if (rasterization_config.conservative_rasterization) {
        VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_create_info{};
        conservative_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
        conservative_create_info.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
        conservative_create_info.extraPrimitiveOverestimationSize = 0.0F;
        p_pipeline_create_infos.conservative_rasterization_state = conservative_create_info;
    }
    p_pipeline_create_infos.rasterization_state = create_info;
}

void ROGraphicsPipelineVK::setup_mulitsample_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    (void)p_config; // TODO: Multi-sample config
    VkPipelineMultisampleStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    create_info.sampleShadingEnable = VK_FALSE;
    create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    p_pipeline_create_infos.multisample_state = create_info;
}

void ROGraphicsPipelineVK::setup_depth_stencil_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& depth_stencil_config = p_config.depth_test;
    VkPipelineDepthStencilStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    create_info.depthTestEnable = depth_stencil_config.depth_test_enabled;
    create_info.depthWriteEnable = depth_stencil_config.depth_write_enabled;
    create_info.depthCompareOp = depth_stencil_config.depth_compare_op;
    create_info.stencilTestEnable = VK_FALSE;
    p_pipeline_create_infos.depth_stencil_state = create_info;
}

void ROGraphicsPipelineVK::setup_color_blend_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& color_blend_config = p_config.color_blend;
    VkPipelineColorBlendStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    create_info.logicOpEnable = color_blend_config.logic_op_enabled;
    create_info.logicOp = color_blend_config.logic_op;
    create_info.attachmentCount = color_blend_config.attachments.size();
    create_info.pAttachments = color_blend_config.attachments.data();
    p_pipeline_create_infos.color_blend_state = create_info;
}

void ROGraphicsPipelineVK::setup_dynamic_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& dynamic_state_config = p_config.dynamic_state;
    VkPipelineDynamicStateCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    create_info.dynamicStateCount = dynamic_state_config.dynamic_states.size();
    create_info.pDynamicStates = dynamic_state_config.dynamic_states.data();
    p_pipeline_create_infos.dynamic_state = create_info;
}

void ROGraphicsPipelineVK::setup_pipeline_rendering_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos) {
    const auto& pipeline_rendering_create_info = p_config.attachment_config;
    VkPipelineRenderingCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    create_info.colorAttachmentCount = pipeline_rendering_create_info.color_formats.size();
    create_info.pColorAttachmentFormats = pipeline_rendering_create_info.color_formats.data();
    create_info.depthAttachmentFormat = WBE_REQUIRED(pipeline_rendering_create_info.depth_formats);
    create_info.stencilAttachmentFormat = WBE_REQUIRED(pipeline_rendering_create_info.stencil_formats);
    p_pipeline_create_infos.pipeline_rendering = create_info;
}

void ROGraphicsPipelineVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_PIPELINE, get_pipeline(), p_name);
}

} // namespace WhiteBirdEngine

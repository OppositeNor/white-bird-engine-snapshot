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
#include "i_svo_render_feature.hh"
#include "core/allocator/i_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "dynamic_mesh_render_feature.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/compute_pipeline_compiler_vk.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/rend_graph_recipe_params_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "generated/serializables_sd.gen.hh"
#include "i_voxel_render_feature.hh"
#include "svo_recipe_params.hh"
#include "resource/resource_manager/resource_manager.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

ISVORenderFeature::ISVORenderFeature(RendererVK* p_renderer,
    Ref<MeshResourcesVK> p_static_mesh_resources,
    const RUAABB3D& p_scene_bounds,
    std::string_view p_shader_prefix,
    std::string_view p_dynamic_shader_prefix,
    bool p_rebuild_static_every_frame)
    : renderer_vk(p_renderer), render_context(nullptr), render_allocator(nullptr),
      static_mesh_resources(std::move(p_static_mesh_resources)), svo_volume(VoxelVolume::enclose(p_scene_bounds, SVO_EXTENT)), rebuild_static_every_frame(p_rebuild_static_every_frame),
      shader_prefix(p_shader_prefix), dynamic_shader_prefix(p_dynamic_shader_prefix) {
    if (renderer_vk == nullptr || static_mesh_resources == MEM_NULL) {
        throw std::runtime_error("ISVORenderFeature: renderer and scene are required.");
    }
    render_context = renderer_vk->get_render_context();
    render_allocator = render_context->allocator;
    compile_pipelines(p_shader_prefix, p_dynamic_shader_prefix);
    create_resources();
}

Ref<ROComputePipelineVK> ISVORenderFeature::compile_compute_pipeline(std::string_view p_resource_id) const {
    ComputePipelineCompilerVK compiler(ComputePipelineCompilerVKContext{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
    });
    return compiler.compile(p_resource_id, ResourceManager::get_singleton()->get_resource<RUShader>(p_resource_id)).compute_pipeline;
}

void ISVORenderFeature::compile_pipelines(std::string_view p_shader_prefix, std::string_view p_dynamic_shader_prefix) {
    auto* resource_manager = ResourceManager::get_singleton();
    GraphicsPipelineCompilerVK svo_compiler(GraphicsPipelineCompilerVKContext{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
        .renderer = renderer_vk,
    });
    const std::string build_pipeline_id = std::format("{}_build_static_mesh_pipeline", p_shader_prefix);
    svo_build_graphics_pipeline =
        svo_compiler.compile(build_pipeline_id, resource_manager->get_resource<RUGraphicsPipeline>(build_pipeline_id)).graphics_pipeline;
    const std::string dynamic_build_pipeline_id = std::format("{}_build_static_mesh_pipeline", p_dynamic_shader_prefix);
    dynamic_build_pipeline =
        svo_compiler
            .compile(dynamic_build_pipeline_id, resource_manager->get_resource<RUGraphicsPipeline>(dynamic_build_pipeline_id))
            .graphics_pipeline;
    dynamic_clear_pipeline = compile_compute_pipeline(std::format("{}_clear_shader", p_dynamic_shader_prefix));
    dynamic_ping_pipeline =
        compile_compute_pipeline(std::format("{}_build_deferred_propagation_pong_write_shader", p_dynamic_shader_prefix));
    dynamic_pong_pipeline =
        compile_compute_pipeline(std::format("{}_build_deferred_propagation_ping_write_shader", p_dynamic_shader_prefix));
    svo_reset_compute_pipeline = compile_compute_pipeline(std::format("{}_reset_shader", p_shader_prefix));
    svo_deferred_count_reset_ping_compute_pipeline = compile_compute_pipeline("svo_deferred_count_reset_ping_shader");
    svo_deferred_count_reset_pong_compute_pipeline = compile_compute_pipeline("svo_deferred_count_reset_pong_shader");
    svo_deferred_propagation_ping_write_compute_pipeline =
        compile_compute_pipeline(std::format("{}_build_deferred_propagation_ping_write_shader", p_shader_prefix));
    svo_deferred_propagation_pong_write_compute_pipeline =
        compile_compute_pipeline(std::format("{}_build_deferred_propagation_pong_write_shader", p_shader_prefix));
    svo_pre_deferred_debug_capture_compute_pipeline = compile_compute_pipeline("svo_pre_deferred_debug_capture_shader");
    svo_debug_draw_setup_compute_pipeline = compile_compute_pipeline("svo_debug_draw_setup_shader");
    GraphicsPipelineCompilerVKContext svo_debug_draw_context{
        .allocator = render_allocator,
        .render_data = render_context,
        .render_database = renderer_vk->get_render_database(),
        .renderer = renderer_vk,
        .color_attachment_formats = {render_context->swap_chain_format},
        .depth_attachment_format = GBUFFER_DEPTH_FORMAT,
        .primitive_topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    };
    svo_debug_draw_graphics_pipeline =
        GraphicsPipelineCompilerVK(svo_debug_draw_context)
            .compile("svo_debug_draw_pipeline", resource_manager->get_resource<RUGraphicsPipeline>("svo_debug_draw_pipeline"))
            .graphics_pipeline;
    svo_pre_deferred_debug_draw_graphics_pipeline =
        GraphicsPipelineCompilerVK(svo_debug_draw_context)
            .compile("svo_pre_deferred_debug_draw_pipeline",
                resource_manager->get_resource<RUGraphicsPipeline>("svo_pre_deferred_debug_draw_pipeline"))
            .graphics_pipeline;
}

void ISVORenderFeature::create_resources() {
    svo_info = render_context->create_buffer(ROBufferVK::Builder().set_properties(sizeof(SVOInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    svo_nodes =
        render_context->create_buffer(ROBufferVK::Builder().set_properties(sizeof(SVONode), SVO_MAX_NODE_COUNT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    dynamic_leaves = render_context->create_buffer(ROBufferVK::Builder()
                         .set_properties(sizeof(SVODynamicLeafBlock), SVO_MAX_DYNAMIC_LEAVES_COUNT / 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    svo_leaves =
        render_context->create_buffer(ROBufferVK::Builder().set_properties(sizeof(SVOLeaf), SVO_MAX_LEAVES_COUNT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    svo_debug_draw_index_buffer = create_initialized_buffer(
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VOXEL_DEBUG_CUBE_INDICES.data(), sizeof(VOXEL_DEBUG_CUBE_INDICES));
    const VkDrawIndexedIndirectCommand initial_debug_draw_command{
        .indexCount = VOXEL_DEBUG_CUBE_INDICES.size(),
        .instanceCount = 0,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0,
    };
    svo_debug_draw_indirect_buffer = create_initialized_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        &initial_debug_draw_command,
        sizeof(initial_debug_draw_command));
    svo_pre_deferred_debug_draw_indirect_buffer =
        create_initialized_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            &initial_debug_draw_command,
            sizeof(initial_debug_draw_command));
    initialize();
}

void ISVORenderFeature::initialize() {
    const SVOInfo initial_svo_info{
        .origin = svo_volume.origin,
        .width = svo_volume.width,
        .max_depth = SVO_MAX_DEPTH,
        .max_node_count = SVO_MAX_NODE_COUNT,
        .max_leaves_count = SVO_MAX_LEAVES_COUNT,
        .node_count = 8,
        .leaves_count = 0,
        .data_count = 8,
        .max_deferred_task_count = SVO_MAX_DEFERRED_TASK_COUNT,
        .max_dynamic_node_count = SVO_MAX_DYNAMIC_NODE_COUNT,
        .max_dynamic_leaves_count = SVO_MAX_DYNAMIC_LEAVES_COUNT,
        .dynamic_node_count = 0,
        .dynamic_leaves_count = 0,
    };
    std::array<SVONode, 8> first_sectors{};
    for (uint32_t i = 0; i < first_sectors.size(); ++i) {
        first_sectors[i] = SVONode{
            .children_data = static_cast<uint32_t>(SVONode::ChildrenState::EMPTY) << 29U,
            .data_index = i,
            .parent_index = -1,
        };
    }
    svo_info->update_buffer(&initial_svo_info, sizeof(initial_svo_info));
    svo_nodes->update_buffer(first_sectors.data(), sizeof(first_sectors));
}

Ref<ROBufferVK> ISVORenderFeature::create_initialized_buffer(VkBufferUsageFlags p_usage, const void* p_data, size_t p_size) const {
    return render_context->create_buffer(ROBufferVK::Builder().set_usage(p_usage).initial_data(p_data, p_size));
}

const char* ISVORenderFeature::get_debug_view_name(VoxelDebugViewMode p_mode) const {
    switch (p_mode) {
    case VoxelDebugViewMode::DISABLED:
        return "Disabled";
    case VoxelDebugViewMode::POST_SVO_BUILD:
        return "Post SVO build";
    case VoxelDebugViewMode::PRE_DEFERRED_PROPAGATION:
        return "Pre deferred propagation";
    }
    throw std::runtime_error("ISVORenderFeature: unknown voxel debug view mode.");
}

void ISVORenderFeature::configure_recipe(
    DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports, const VoxelRecipeInputs& p_inputs) const {
    SVORecipeParams params;
    params.module = "render_graph_utils.features.voxel_svo";
    params.variant = get_recipe_variant();
    params.shader_prefix = shader_prefix;
    params.dynamic_shader_prefix = dynamic_shader_prefix;
    params.extent = SVO_EXTENT;
    params.max_depth = SVO_MAX_DEPTH;
    params.reset_group_count = get_reset_group_count();
    params.deferred_dispatch_group_count = SVO_DEFERRED_DISPATCH_GROUP_COUNT;
    params.dynamic_clear_group_count = (SVO_MAX_NODE_COUNT + 127) / 128;
    // The static publication runs one invocation per node, the dynamic one per static and dynamic node.
    params.static_publication_group_count = (SVO_MAX_NODE_COUNT + 63) / 64;
    params.dynamic_publication_group_count = (SVO_MAX_NODE_COUNT + SVO_MAX_DYNAMIC_NODE_COUNT + 127) / 128;
    // Byte sizes of the scratch buffers the module declares, from the capacities svo_info records.
    params.buffer_sizes = SVOBufferSizes{
        .dynamic_nodes = sizeof(SVONode) * SVO_MAX_DYNAMIC_NODE_COUNT,
        .deferred_list = sizeof(uint32_t) * SVO_DEFERRED_LIST_ELEMENT_COUNT,
        .pre_deferred_debug_info = sizeof(SVOInfo),
        .pre_deferred_debug_list = sizeof(uint32_t) * SVO_MAX_DEFERRED_TASK_COUNT,
    };
    if (!rebuild_static_every_frame) {
        params.static_build = RendRecipeTriggerVK{.event = std::string(p_inputs.static_build_event), .activate = p_inputs.static_build_pending};
    }
    // The buffers holding initial contents or state shared by both graphs; the module declares the per-frame scratch buffers.
    const std::array<std::pair<std::string_view, const Ref<ROBufferVK>*>, 7> buffers{
        {
            {SVO_INFO, &svo_info},
            {SVO_NODE_BUFFER, &svo_nodes},
            {SVO_DYNAMIC_LEAVES, &dynamic_leaves},
            {SVO_LEAF_BUFFER, &svo_leaves},
            {SVO_DEBUG_DRAW_INDEX_BUFFER, &svo_debug_draw_index_buffer},
            {SVO_DEBUG_DRAW_INDIRECT_BUFFER, &svo_debug_draw_indirect_buffer},
            {SVO_PRE_DEFERRED_DEBUG_DRAW_INDIRECT_BUFFER, &svo_pre_deferred_debug_draw_indirect_buffer},
        },
    };
    for (const auto& [name, buffer] : buffers) {
        p_imports.add(name, **buffer);
        params.imports.emplace_back(name);
    }
    SerializableSD<SVORecipeParams>::serialize(p_parameters, params);
}

} // namespace WhiteBirdEngine

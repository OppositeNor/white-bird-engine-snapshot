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
#include "dynamic_mesh_render_feature.hh"
#include "../render_test.hh"
#include "/home/oppositenormal/Documents/programming/white-bird-engine/tests/executable/function/render/entity_draw_data.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/rt_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

DynamicMeshRenderFeature::DynamicMeshRenderFeature(
    RendererVK* p_renderer, RefRes<RUMesh3D> p_mesh, RefRes<RUGraphicsPipeline> p_material_pipeline, size_t p_entity_count)
    : render_data(p_renderer->get_render_context()) {
    if (p_entity_count == 0) {
        throw std::runtime_error("Dynamic mesh feature requires at least one entity.");
    }
    resources = create_mesh_resources(p_renderer, DYNAMIC_MESH_KEY, p_mesh, p_material_pipeline);
    entities.reserve(p_entity_count);
    entity_keys.reserve(p_entity_count);
    for (size_t i = 0; i < p_entity_count; ++i) {
        entities.push_back(make_unique<EntityDrawData>(render_data->allocator, render_data, resources));
        entity_keys.push_back(std::format("{}_{}", DYNAMIC_MESH_ENTITY_KEY, i));
    }
}

void DynamicMeshRenderFeature::register_resources(RendGraphDescVK& p_graph) const {
    for (size_t i = 0; i < entities.size(); ++i) {
        p_graph.add_resource(entity_keys[i], *entities[i]);
    }
}

void DynamicMeshRenderFeature::set_transform(size_t p_entity_index, const glm::mat4& p_transform) {
    if (p_entity_index >= entities.size()) {
        throw std::runtime_error("Dynamic mesh entity index is out of range.");
    }
    entities[p_entity_index]->set_transform(p_transform);
}

std::string DynamicMeshRenderFeature::get_entity_transform_id(size_t p_entity_index) const {
    if (p_entity_index >= entities.size()) {
        throw std::runtime_error("Dynamic mesh entity index is out of range.");
    }
    return IBindableRenderResourceVK::make_member_resource_id(entity_keys[p_entity_index], EntityDrawData::TRANSFORM);
}

RTParamsGraphicsDrawMeshVK DynamicMeshRenderFeature::make_draw_params(
    size_t p_entity_index, std::string_view p_pipeline_key, const RendExtent2DParamsVK& p_extent) const {
    return RTParamsGraphicsDrawMeshVK{
        .mesh = std::string(DYNAMIC_MESH_KEY),
        .pipeline = RTParamsGraphicsDrawMeshVK::make_default_pipeline(p_pipeline_key),
        .parts = {MeshPartVK::OPAQUE},
        .transform = get_entity_transform_id(p_entity_index),
        .render_extent = p_extent,
        .color_load_op = AttachmentLoadOpVK::LOAD,
        .depth_load_op = AttachmentLoadOpVK::LOAD,
    };
}

RendTaskDescVK& DynamicMeshRenderFeature::add_task(RendGraphDescVK& p_graph,
    std::string_view p_task_id,
    size_t p_entity_index,
    std::string_view p_pipeline_key,
    const RendExtent2DParamsVK& p_extent,
    std::span<const std::string_view> p_colors,
    std::optional<std::string_view> p_depth) const {
    RendTaskDescVK& task = RTGraphicsDrawMeshVK::add_to_graph(
        p_graph, p_task_id, make_draw_params(p_entity_index, p_pipeline_key, p_extent), render_data);
    return alias_render_targets(task, p_colors, p_depth);
}

} // namespace WhiteBirdEngine

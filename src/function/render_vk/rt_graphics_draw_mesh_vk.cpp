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
#include "function/render_vk/rt_graphics_draw_mesh_vk.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/render_database.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/mesh_compiler_vk.hh"
#include "function/render_vk/mesh_draw_batch_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_task_create_utils_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "generated/enum_reflection.gen.hh"
#include "generated/type_uuid.gen.hh"
#include <algorithm>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

namespace {

constexpr std::string_view TASK_NAME = "RTGraphicsDrawMeshVK";

std::string mesh_member(const RTParamsGraphicsDrawMeshVK& p_params, std::string_view p_member) {
    return IBindableRenderResourceVK::make_member_resource_id(p_params.mesh, p_member);
}

bool pipeline_declares(const ROGraphicsPipelineVK& p_pipeline, std::string_view p_resource_id) {
    return std::ranges::any_of(p_pipeline.get_descriptor_layout().fields, [p_resource_id](const auto& p_field) {
        return p_field.resource_id == p_resource_id;
    });
}

void validate_parts(const RTParamsGraphicsDrawMeshVK& p_params) {
    if (p_params.parts.empty()) {
        throw std::runtime_error(std::format("{}: at least one mesh part is required.", TASK_NAME));
    }
    for (size_t i = 0; i < p_params.parts.size(); ++i) {
        if (std::find(p_params.parts.begin(), p_params.parts.begin() + static_cast<std::ptrdiff_t>(i), p_params.parts[i]) !=
            p_params.parts.begin() + static_cast<std::ptrdiff_t>(i)) {
            throw std::runtime_error(std::format(
                "{}: mesh part '{}' is listed more than once.", TASK_NAME, EnumReflection<MeshPartVK>::enum_to_string(p_params.parts[i])));
        }
    }
}

Ref<MeshResourcesVK> acquire_registered_mesh(const RenderContextVK* p_render_data, std::string_view p_mesh_key) {
    Ref<MeshResourcesVK> mesh = p_render_data->render_database->get_compiled_render_resource(p_mesh_key, TypeUUID<MeshResourcesVK>::UUID)
                                    .dynamic_cast_ref<MeshResourcesVK>();
    if (mesh == MEM_NULL) {
        throw std::runtime_error(std::format(
            "{}: no compiled mesh is registered under '{}'; add the task with add_to_graph() or RenderTaskRegistryVK::add_task().",
            TASK_NAME,
            p_mesh_key));
    }
    return mesh;
}

} // namespace

RTGraphicsDrawMeshVK::RTGraphicsDrawMeshVK(Unique<RTRunGraphicsPipelineIndirectVK> p_draw) : draw(std::move(p_draw)) {
    if (draw.is_null()) {
        throw std::runtime_error(std::format("{}: the indirect draw is null.", TASK_NAME));
    }
}

Unique<IRenderTaskVK> RTGraphicsDrawMeshVK::create(
    const RenderContextVK* p_render_data, const RTParamsGraphicsDrawMeshVK& p_params, const RendGraphContextVK& p_context) {
    validate_parts(p_params);
    Ref<ROGraphicsPipelineVK> pipeline = acquire_task_graphics_pipeline_vk(p_render_data, TASK_NAME, p_params.pipeline);
    Ref<MeshResourcesVK> mesh = acquire_registered_mesh(p_render_data, p_params.mesh);
    if (pipeline->get_mesh_node_contract().has_value()) {
        MeshResourcesVK::validate_node_contract(*pipeline->get_mesh_node_contract());
    }

    RTRunGraphicsPipelineIndirectVK::Builder builder(p_render_data->allocator);
    for (size_t i = 0; i < mesh->get_vertex_buffer_count(); ++i) {
        builder.add_vertex_buffer(mesh_member(p_params, MeshResourcesVK::get_vertex_buffer_member(i)));
    }
    builder.set_index_buffer(mesh_member(p_params, MeshResourcesVK::INDICES));
    const VertexInputRequirement& requirement = pipeline->get_vertex_input_requirement();
    for (MeshPartVK part : p_params.parts) {
        if (mesh->acquire_draw_commands(part) == MEM_NULL || mesh->get_draw_batch_count(part) == 0) {
            throw std::runtime_error(std::format(
                "{}: mesh '{}' has no {} submeshes.", TASK_NAME, p_params.mesh, EnumReflection<MeshPartVK>::enum_to_string(part)));
        }
        const uint32_t indirect_buffer_index =
            builder.add_indirect_buffer(mesh_member(p_params, MeshResourcesVK::get_draw_commands_member(part)));
        for (size_t batch_index = 0; batch_index < mesh->get_draw_batch_count(part); ++batch_index) {
            const MeshDrawBatchVK& source = mesh->get_draw_batch(part, batch_index);
            RTRunGraphicsPipelineIndirectVK::PipelineBatch batch;
            batch.graphics_pipeline = pipeline;
            batch.indirect_buffer_index = indirect_buffer_index;
            batch.first_draw = source.first_draw;
            batch.draw_count = source.draw_count;
            batch.vertex_input = source.resolve_vertex_input(requirement, batch.vertex_buffers);
            builder.add_pipeline_batch(std::move(batch));
        }
    }
    std::vector<VkClearValue> clear_values;
    clear_values.reserve(p_params.clear_values.size());
    for (const RTParamsClearValueVK& clear_value : p_params.clear_values) {
        clear_values.push_back(clear_value.to_vk());
    }
    builder.set_render_extent(p_params.render_extent.resolve(p_context))
        .set_clear_values(clear_values)
        .set_color_load_op(to_vk_attachment_load_op(p_params.color_load_op))
        .set_depth_load_op(to_vk_attachment_load_op(p_params.depth_load_op));
    return make_unique<RTGraphicsDrawMeshVK>(p_render_data->allocator, builder.create_unique(p_render_data));
}

void RTGraphicsDrawMeshVK::prepare_graph(
    RendGraphDescVK& p_graph, RendTaskDescVK& p_task, const RTParamsGraphicsDrawMeshVK& p_params, const RenderContextVK* p_render_data) {
    validate_parts(p_params);
    Ref<ROGraphicsPipelineVK> pipeline = acquire_task_graphics_pipeline_vk(p_render_data, TASK_NAME, p_params.pipeline);
    RendererVK* renderer = RendererVK::get_singleton();
    if (renderer == nullptr) {
        throw std::runtime_error(std::format("{}: the Vulkan renderer is unavailable to compile mesh '{}'.", TASK_NAME, p_params.mesh));
    }
    Ref<MeshResourcesVK> mesh = MeshCompilerVK(renderer).acquire(p_params.mesh);
    if (!p_graph.contains_resource(mesh_member(p_params, MeshResourcesVK::INDICES))) {
        p_graph.add_resource(p_params.mesh, *mesh);
    }

    const auto alias_declared = [&p_task, &pipeline](std::string_view p_shader_id, std::string_view p_graph_id) {
        if (pipeline_declares(*pipeline, p_shader_id)) {
            p_task.alias(p_shader_id, p_graph_id);
        }
    };
    alias_declared(REND_RES_MESH_SUBMESHES, mesh_member(p_params, MeshResourcesVK::SUBMESHES));
    alias_declared(REND_RES_MESH_NODES, mesh_member(p_params, MeshResourcesVK::NODES));
    alias_declared(REND_RES_MESH_INSTANCES, mesh_member(p_params, MeshResourcesVK::INSTANCES));
    alias_declared(REND_RES_MESH_MATERIAL_SAMPLER, mesh_member(p_params, MeshResourcesVK::MATERIAL_SAMPLER));
    if (pipeline_declares(*pipeline, REND_RES_MESH_TRANSFORM)) {
        if (!p_params.transform.has_value() || p_params.transform->empty()) {
            throw std::runtime_error(std::format(
                "{}: pipeline '{}' reads the whole-mesh transform, but no transform resource is set.", TASK_NAME, p_params.pipeline));
        }
        p_task.alias(REND_RES_MESH_TRANSFORM, *p_params.transform);
    }
    if (pipeline_declares(*pipeline, REND_RES_MESH_MATERIAL_PARAMS)) {
        if (!pipeline->get_material_texture_contract().has_value()) {
            throw std::runtime_error(std::format(
                "{}: pipeline '{}' reads a material table but declares no material texture contract.", TASK_NAME, p_params.pipeline));
        }
        const size_t table_index = mesh->acquire_material_table(*pipeline->get_material_texture_contract());
        const std::string table_id = mesh_member(p_params, MeshResourcesVK::get_material_params_member(table_index));
        if (!p_graph.contains_resource(table_id)) {
            mesh->acquire_material_params(table_index)->bind(p_graph, table_id);
        }
        p_task.alias(REND_RES_MESH_MATERIAL_PARAMS, table_id);
    }
    if (p_task.factory == nullptr) {
        throw std::runtime_error(std::format("{}: task '{}' has no factory to prepare.", TASK_NAME, p_task.task_id));
    }
    // The RenderDatabase holds compiled objects weakly; the task descriptor keeps the ones create() looks up alive.
    p_task.factory = [factory = std::move(p_task.factory), pipeline, mesh](const RendGraphContextVK& p_context) {
        return factory(p_context);
    };
}

RendTaskDescVK& RTGraphicsDrawMeshVK::add_to_graph(
    RendGraphDescVK& p_graph, std::string_view p_task_id, const RTParamsGraphicsDrawMeshVK& p_params, const RenderContextVK* p_render_data) {
    if (std::ranges::any_of(p_graph.tasks, [p_task_id](const RendTaskDescVK& p_task) {
            return p_task.task_id == p_task_id;
        })) {
        throw std::runtime_error(std::format("{}: the graph already has a task with id '{}'.", TASK_NAME, p_task_id));
    }
    RendTaskDescVK task(p_graph.allocator, p_task_id, [p_render_data, p_params](const RendGraphContextVK& p_context) {
        return create(p_render_data, p_params, p_context);
    });
    prepare_graph(p_graph, task, p_params, p_render_data);
    p_graph.add_task(std::move(task));
    return p_graph.get_task(p_task_id);
}

void RTGraphicsDrawMeshVK::append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const {
    draw->append_accesses(p_reads, p_writes);
}

void RTGraphicsDrawMeshVK::bind_resources(const RendResViewVK& p_resources) {
    draw->bind_resources(p_resources);
}

void RTGraphicsDrawMeshVK::record(VkCommandBuffer p_command_buffer, double p_delta) {
    draw->record(p_command_buffer, p_delta);
}

} // namespace WhiteBirdEngine

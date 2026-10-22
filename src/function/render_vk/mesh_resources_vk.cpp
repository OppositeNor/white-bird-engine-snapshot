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
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render/render_database.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/mesh_node_contract.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/mesh_gpu_data_vk.hh"
#include "function/render_vk/mesh_material_resource_builder_vk.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "generated/type_uuid.gen.hh"
#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

namespace {

RenderAllocator* get_render_allocator(const RenderContextVK* p_render_data) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("Failed to construct mesh resources: render data or its allocator is null.");
    }
    return p_render_data->allocator;
}

Ref<ROBufferVK> create_material_params_buffer(const RenderContextVK* p_render_data, std::span<const uint8_t> p_buffer_data) {
    const DescriptorArenaLocationVK arena = p_render_data->buffer_descriptor_arena;
    uint32_t uniform_view_index = p_render_data->descriptor_heap_allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
    Ref<ROBufferVK> result = p_render_data->create_buffer(ROBufferVK::Builder()
                                 .set_domain(DeviceDomain::HOST_DEVICE)
                                 .set_size(p_buffer_data.size())
                                 .set_usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
                                 .set_uniform_view_index(uniform_view_index));
    result->update_buffer(p_buffer_data.data(), p_buffer_data.size());
    return result;
}

} // namespace

MeshResourcesVK::MeshResourcesVK(const RenderContextVK* p_render_data)
    : IRenderObjectVK(p_render_data), allocator(get_render_allocator(p_render_data)), vertex_buffers(allocator),
      vertex_binaries(allocator), textures(make_ref<RenderResourceListVK>(allocator, allocator)), opaque_batches(allocator),
      opaque_double_sided_batches(allocator), masked_batches(allocator) {
}

TypeID MeshResourcesVK::get_type_uuid() const {
    return TypeUUID<MeshResourcesVK>::UUID;
}

std::string MeshResourcesVK::get_vertex_buffer_member(size_t p_index) {
    return std::format("{}.{}", VERTICES, p_index);
}

std::string MeshResourcesVK::get_material_params_member(size_t p_index) {
    return std::format("{}.{}", MATERIAL_PARAMS, p_index);
}

std::string_view MeshResourcesVK::get_draw_commands_member(MeshPartVK p_part) {
    switch (p_part) {
    case MeshPartVK::OPAQUE:
        return OPAQUE_DRAW_BATCHES;
    case MeshPartVK::MASKED:
        return MASKED_DRAW_BATCHES;
    case MeshPartVK::DOUBLE_SIDED:
        return OPAQUE_DOUBLE_SIDED_DRAW_BATCHES;
    }
    throw std::runtime_error("Failed to get mesh draw commands member: the mesh part is invalid.");
}

void MeshResourcesVK::validate_node_contract(const MeshNodeContract& p_contract) {
    MeshNodeContract cpu_contract;
    cpu_contract.element_stride = sizeof(GeometryNodeGPU);
    cpu_contract.transform_offset = offsetof(GeometryNodeGPU, transform);
    cpu_contract.normal_transform_offset = offsetof(GeometryNodeGPU, normal_transform);
    if (cpu_contract.matches(p_contract)) {
        return;
    }
    throw std::runtime_error(std::format("Mesh node layout mismatch: shader expects stride={}, transform offset={}, normal_transform offset={}, but CPU uploads stride={}, transform offset={}, normal_transform offset={}.",
        p_contract.element_stride,
        p_contract.transform_offset,
        p_contract.normal_transform_offset,
        cpu_contract.element_stride,
        cpu_contract.transform_offset,
        cpu_contract.normal_transform_offset));
}

Ref<ROBufferVK> MeshResourcesVK::acquire_vertex_buffer(size_t p_index) const {
    if (p_index >= vertex_buffers.size()) {
        throw std::runtime_error("Failed to acquire static-mesh vertex buffer: index is out of bounds.");
    }
    return vertex_buffers[p_index];
}

size_t MeshResourcesVK::get_vertex_buffer_index(RefRes<RUBinary> p_binary) const {
    for (size_t i = 0; i < vertex_binaries.size(); ++i) {
        if (vertex_binaries[i] == p_binary) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find static-mesh vertex buffer: no vertex buffer was compiled from the binary.");
}

Ref<ROBufferVK> MeshResourcesVK::acquire_draw_commands(MeshPartVK p_part) const {
    switch (p_part) {
    case MeshPartVK::OPAQUE:
        return opaque_draw_batches;
    case MeshPartVK::MASKED:
        return masked_draw_batches;
    case MeshPartVK::DOUBLE_SIDED:
        return opaque_double_sided_draw_batches;
    }
    throw std::runtime_error("Failed to acquire mesh draw commands: the mesh part is invalid.");
}

size_t MeshResourcesVK::get_draw_batch_count(MeshPartVK p_part) const {
    switch (p_part) {
    case MeshPartVK::OPAQUE:
        return opaque_batches.size();
    case MeshPartVK::MASKED:
        return masked_batches.size();
    case MeshPartVK::DOUBLE_SIDED:
        return opaque_double_sided_batches.size();
    }
    throw std::runtime_error("Failed to count mesh draw batches: the mesh part is invalid.");
}

const MeshDrawBatchVK& MeshResourcesVK::get_draw_batch(MeshPartVK p_part, size_t p_index) const {
    switch (p_part) {
    case MeshPartVK::OPAQUE:
        return get_opaque_draw_batch(p_index);
    case MeshPartVK::MASKED:
        return get_masked_draw_batch(p_index);
    case MeshPartVK::DOUBLE_SIDED:
        return get_opaque_double_sided_draw_batch(p_index);
    }
    throw std::runtime_error("Failed to get mesh draw batch: the mesh part is invalid.");
}

const MeshDrawBatchVK& MeshResourcesVK::get_opaque_draw_batch(size_t p_index) const {
    if (p_index >= opaque_batches.size()) {
        throw std::runtime_error("Failed to get static-mesh opaque draw batch: index is out of bounds.");
    }
    return opaque_batches[p_index];
}

const MeshDrawBatchVK& MeshResourcesVK::get_opaque_double_sided_draw_batch(size_t p_index) const {
    if (p_index >= opaque_double_sided_batches.size()) {
        throw std::runtime_error("Failed to get static-mesh opaque_double_sided draw batch: index is out of bounds.");
    }
    return opaque_double_sided_batches[p_index];
}

const MeshDrawBatchVK& MeshResourcesVK::get_masked_draw_batch(size_t p_index) const {
    if (p_index >= masked_batches.size()) {
        throw std::runtime_error("Failed to get static-mesh masked draw batch: index is out of bounds.");
    }
    return masked_batches[p_index];
}

size_t MeshResourcesVK::acquire_material_table(const MaterialTextureContract& p_contract) {
    for (size_t i = 0; i < material_tables.size(); ++i) {
        if (material_tables[i].contract.matches(p_contract)) {
            return i;
        }
    }
    MeshMaterialResourcesVK material_resources = MeshMaterialResourceBuilderVK().build(p_contract,
        materials,
        default_material,
        render_data->sampled_image_descriptor_capacity,
        material_sampler,
        [this](RefRes<RUTexture> p_texture) {
            for (const auto& [texture, image_view] : texture_views) {
                if (texture == p_texture) {
                    return image_view->get_sampled_view_index();
                }
            }
            throw std::runtime_error("Failed to build mesh material table: a material texture was not compiled with the mesh.");
        });
    if (material_resources.material_params_data.empty()) {
        throw std::runtime_error("Failed to build mesh material table: the material parameter buffer is empty.");
    }
    material_tables.push_back(MaterialTable{
        .contract = p_contract,
        .buffer = create_material_params_buffer(render_data, material_resources.material_params_data),
    });
    if (!get_debug_name().empty() && render_data->render_database != nullptr) {
        render_data->render_database->register_compiled_render_resource(
            make_member_resource_id(get_debug_name(), get_material_params_member(material_tables.size() - 1)), material_tables.back().buffer);
    }
    return material_tables.size() - 1;
}

Ref<ROBufferVK> MeshResourcesVK::acquire_material_params(size_t p_index) const {
    if (p_index >= material_tables.size()) {
        throw std::runtime_error("Failed to acquire mesh material parameter table: index is out of bounds.");
    }
    return material_tables[p_index].buffer;
}

void MeshResourcesVK::apply_debug_name(std::string_view p_name) {
    if (render_data->render_database == nullptr) {
        return;
    }
    const auto name_member = [this, p_name](const auto& p_object, std::string_view p_member) {
        if (p_object != MEM_NULL) {
            render_data->render_database->register_compiled_render_resource(make_member_resource_id(p_name, p_member), p_object);
        }
    };
    name_member(submeshes, SUBMESHES);
    name_member(nodes, NODES);
    name_member(instances, INSTANCES);
    name_member(opaque_draw_batches, OPAQUE_DRAW_BATCHES);
    name_member(opaque_double_sided_draw_batches, OPAQUE_DOUBLE_SIDED_DRAW_BATCHES);
    name_member(masked_draw_batches, MASKED_DRAW_BATCHES);
    name_member(material_sampler, MATERIAL_SAMPLER);
    for (size_t i = 0; i < material_tables.size(); ++i) {
        name_member(material_tables[i].buffer, get_material_params_member(i));
    }
}

void MeshResourcesVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    if (vertex_buffers.empty() || indices == MEM_NULL || submeshes == MEM_NULL || nodes == MEM_NULL || instances == MEM_NULL) {
        throw std::runtime_error("Failed to bind static-mesh resources: geometry or instance tables are missing.");
    }
    const auto member = [p_resource_id](std::string_view p_member) {
        return make_member_resource_id(p_resource_id, p_member);
    };
    for (size_t i = 0; i < vertex_buffers.size(); ++i) {
        vertex_buffers[i]->bind(p_graph, member(get_vertex_buffer_member(i)));
    }
    indices->bind(p_graph, member(INDICES));
    submeshes->bind(p_graph, member(SUBMESHES));
    nodes->bind(p_graph, member(NODES));
    instances->bind(p_graph, member(INSTANCES));
    textures->bind(p_graph, member(TEXTURES));
    if (opaque_draw_batches != MEM_NULL) {
        opaque_draw_batches->bind(p_graph, member(OPAQUE_DRAW_BATCHES));
    }
    if (opaque_double_sided_draw_batches != MEM_NULL) {
        opaque_double_sided_draw_batches->bind(p_graph, member(OPAQUE_DOUBLE_SIDED_DRAW_BATCHES));
    }
    if (masked_draw_batches != MEM_NULL) {
        masked_draw_batches->bind(p_graph, member(MASKED_DRAW_BATCHES));
    }
    for (size_t i = 0; i < material_tables.size(); ++i) {
        material_tables[i].buffer->bind(p_graph, member(get_material_params_member(i)));
    }
    if (material_sampler != MEM_NULL) {
        material_sampler->bind(p_graph, member(MATERIAL_SAMPLER));
    }
}

} // namespace WhiteBirdEngine

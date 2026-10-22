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
#include "function/render_vk/mesh_compiler_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_raw.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/mesh_buffer_compiler_vk.hh"
#include "function/render_vk/mesh_draw_batch_planner_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "glm/common.hpp"
#include "glm/matrix.hpp"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <cstdint>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_sampler_vk.hh"
#include "resource/resource_manager/resource_manager.hh"
#include <format>
#include <string>
#include "function/render_vk/mesh_gpu_data_vk.hh"
#include "generated/type_uuid.gen.hh"
#include <algorithm>

namespace WhiteBirdEngine {

namespace {

RefRes<RUMaterial> get_default_material(std::string_view p_default_material_key) {
    ResourceManager* resource_manager = ResourceManager::get_singleton();
    if (resource_manager == nullptr || p_default_material_key.empty()) {
        return MEM_NULL;
    }
    return resource_manager->get_resource<RUMaterial>(std::string(p_default_material_key));
}

Ref<ROSamplerVK> create_material_sampler(const RenderContextVK* p_render_data) {
    const DescriptorArenaLocationVK arena = p_render_data->sampler_descriptor_arena;
    uint32_t sampler_index = p_render_data->descriptor_heap_allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
    return p_render_data->create_sampler(ROSamplerVK::Builder().set_sampler_index(sampler_index));
}

Ref<ROBufferVK> create_host_buffer(const RenderContextVK* p_render_data, const void* p_data, size_t p_size, VkBufferUsageFlags p_usage) {
    if (p_size == 0) {
        return MEM_NULL;
    }
    Ref<ROBufferVK> result =
        p_render_data->create_buffer(ROBufferVK::Builder().set_domain(DeviceDomain::HOST_DEVICE).set_size(p_size).set_usage(p_usage));
    result->update_buffer(p_data, p_size);
    return result;
}

struct DrawPlan {
    explicit DrawPlan(RenderAllocator* p_allocator) : commands(p_allocator), batches(p_allocator) {
    }
    ~DrawPlan() = default;
    DrawPlan(const DrawPlan&) = delete;
    DrawPlan(DrawPlan&&) noexcept = default;
    DrawPlan& operator=(const DrawPlan&) = delete;
    DrawPlan& operator=(DrawPlan&&) noexcept = default;

    Vector<VkDrawIndexedIndirectCommand, RenderAllocator> commands;
    Vector<MeshDrawBatchVK, RenderAllocator> batches;
};

void validate_material(RefRes<RUMaterial> p_material) {
    if (p_material == nullptr) {
        throw std::runtime_error("Failed to compile mesh resources: submesh material is missing.");
    }
}

uint32_t get_or_append_material_index(std::vector<RefRes<RUMaterial>>& p_materials, RefRes<RUMaterial> p_material) {
    for (size_t i = 0; i < p_materials.size(); ++i) {
        if (p_materials[i] == p_material) {
            return static_cast<uint32_t>(i);
        }
    }
    if (p_materials.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Failed to compile mesh resources: material table exceeds uint32.");
    }
    p_materials.push_back(p_material);
    return static_cast<uint32_t>(p_materials.size() - 1);
}

Ref<ROBufferVK> create_storage_buffer(const RenderContextVK* p_render_data, const void* p_data, size_t p_size) {
    return create_host_buffer(p_render_data, p_data, p_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

Ref<ROBufferVK> create_indirect_buffer(const RenderContextVK* p_render_data, std::span<const VkDrawIndexedIndirectCommand> p_commands) {
    return create_host_buffer(p_render_data,
        p_commands.data(),
        p_commands.size_bytes(),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

MeshBufferCompilerVK::BuildResult compile_geometry_buffers(
    const RenderContextVK* p_render_data, RefRes<RUMesh3D> p_mesh, const MeshCompilerVK::Config& p_config) {
    return MeshBufferCompilerVK(p_render_data)
        .build(p_mesh,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | p_config.vertex_buffer_usage,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | p_config.index_buffer_usage);
}

DrawPlan plan_draws(
    const RenderContextVK* p_render_data, const MeshResourcesVK& p_resources, std::span<const MeshDrawBatchPlannerVK::Draw> p_draws) {
    MeshDrawBatchPlannerVK::BuildResult draw_plan =
        MeshDrawBatchPlannerVK().build(p_render_data->allocator, p_draws, p_render_data->max_draw_indirect_count);
    DrawPlan result(p_render_data->allocator);
    result.commands = std::move(draw_plan.commands);
    result.batches.reserve(draw_plan.batches.size());
    for (MeshDrawBatchPlannerVK::Batch& source_batch : draw_plan.batches) {
        MeshDrawBatchVK draw_batch;
        draw_batch.first_draw = source_batch.first_draw;
        draw_batch.draw_count = source_batch.draw_count;
        draw_batch.vertex_bindings = source_batch.vertex_bindings;
        draw_batch.vertex_buffers.reserve(source_batch.vertex_binaries.size());
        for (size_t i = 0; i < source_batch.vertex_binaries.size(); ++i) {
            draw_batch.vertex_buffers.push_back(VertexBufferBindingVK{
                .buffer_index = static_cast<uint32_t>(p_resources.get_vertex_buffer_index(source_batch.vertex_binaries[i])),
                .offset = source_batch.vertex_buffer_offsets[i],
            });
        }
        result.batches.push_back(std::move(draw_batch));
    }
    return result;
}

void append_texture_views(GraphicsPipelineCompilerVK& p_texture_compiler,
    std::vector<std::pair<RefRes<RUTexture>, Ref<ROImageViewVK>>>& p_texture_views,
    RefRes<RUMaterial> p_material) {
    if (p_material == nullptr) {
        return;
    }
    if (p_material->texture_count > 0 && p_material->textures == nullptr) {
        throw std::runtime_error("Failed to compile mesh resources: material texture slots are missing.");
    }
    for (size_t i = 0; i < p_material->texture_count; ++i) {
        RefRes<RUTexture> texture = p_material->textures.get()[i].texture;
        if (texture == nullptr || std::ranges::any_of(p_texture_views, [&texture](const auto& p_view) {
                return p_view.first == texture;
            })) {
            continue;
        }
        // Keyed by the texture's resource key, which also names the image and view; anonymous textures by address.
        const std::string texture_key = texture->resource_key != nullptr && texture->resource_key[0] != '\0'
                                            ? std::string(texture->resource_key)
                                            : std::format("texture_resource_{}", reinterpret_cast<uintptr_t>(texture.get()));
        p_texture_views.emplace_back(texture, p_texture_compiler.compile_texture_view(texture_key, texture));
    }
}

struct MaterialBuild {
    RefRes<RUMaterial> default_material = MEM_NULL;
    std::vector<std::pair<RefRes<RUTexture>, Ref<ROImageViewVK>>> texture_views;
    Ref<ROSamplerVK> material_sampler = MEM_NULL;
};

// Compile a sampled view of every texture of every material, so that material tables can later be built for any pipeline's
// material texture contract.
MaterialBuild compile_materials(
    RendererVK* p_renderer, const MeshCompilerVK::Config& p_config, std::span<const RefRes<RUMaterial>> p_materials) {
    const RenderContextVK* render_data = p_renderer->get_render_context();
    GraphicsPipelineCompilerVKContext compiler_context{
        .allocator = render_data->allocator,
        .render_data = render_data,
        .render_database = p_renderer->get_render_database(),
        .renderer = p_renderer,
    };
    GraphicsPipelineCompilerVK texture_compiler(compiler_context);
    MaterialBuild result;
    result.default_material = get_default_material(p_config.default_material_resource_key);
    append_texture_views(texture_compiler, result.texture_views, result.default_material);
    for (const RefRes<RUMaterial>& material : p_materials) {
        append_texture_views(texture_compiler, result.texture_views, material);
    }
    result.material_sampler = create_material_sampler(render_data);
    return result;
}

struct StaticInstanceBufferData {
    std::vector<GeometrySubmeshGPU> submeshes;
    std::vector<GeometryNodeGPU> nodes;
    std::vector<GeometryInstanceGPU> instances;
    std::vector<uint32_t> first_instances;
    std::vector<RefRes<RUMaterial>> ordered_materials;
};

struct CombinedStaticInstanceBufferData {
    StaticInstanceBufferData data;
    std::vector<uint32_t> opaque_first_instances;
    std::vector<uint32_t> opaque_double_sided_first_instances;
    std::vector<uint32_t> masked_first_instances;
};

bool has_draws(const RUSubmeshInstance3D& p_category) {
    return p_category.submesh_count != 0 && p_category.instance_count != 0;
}

template <typename TMaterialIndexer>
StaticInstanceBufferData build_static_instance_buffer_data(const RUSubmeshInstance3D& p_mesh, const TMaterialIndexer& p_material_indexer) {
    StaticInstanceBufferData result;
    result.submeshes.reserve(p_mesh.submesh_count);
    result.nodes.reserve(p_mesh.instance_count);
    result.instances.reserve(p_mesh.instance_count);
    result.first_instances.reserve(p_mesh.submesh_count);
    result.ordered_materials.reserve(p_mesh.submesh_count);
    for (size_t i = 0; i < p_mesh.instance_count; ++i) {
        const glm::mat4& global_transform = p_mesh.instances.get()[i].global_transform;
        const glm::mat3 transform_3x3 = glm::mat3(global_transform);
        glm::mat4 normal_transform{1.0F};
        if (glm::abs(glm::determinant(transform_3x3)) >= 0.000001F) {
            normal_transform = glm::mat4(glm::inverseTranspose(transform_3x3));
        }
        result.nodes.push_back(GeometryNodeGPU{.transform = global_transform, .normal_transform = normal_transform});
    }

    for (size_t submesh_index = 0; submesh_index < p_mesh.submesh_count; ++submesh_index) {
        const RUSubmesh3D& submesh = p_mesh.submeshes.get()[submesh_index];
        if (result.instances.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to compile static-mesh resources: instance table exceeds uint32.");
        }
        result.first_instances.push_back(static_cast<uint32_t>(result.instances.size()));
        if (submesh.indices.index_count == 0 || submesh.instance_count == 0) {
            continue;
        }
        const uint32_t material_index = p_material_indexer(result.ordered_materials, submesh.material);
        result.submeshes.push_back(GeometrySubmeshGPU{.material_id = material_index});
        const uint32_t drawable_submesh_index = static_cast<uint32_t>(result.submeshes.size() - 1);
        for (size_t instance_index = 0; instance_index < submesh.instance_count; ++instance_index) {
            const size_t node_index = submesh.first_instance + instance_index;
            if (node_index > std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("Failed to compile static-mesh resources: node index exceeds uint32.");
            }
            result.instances.push_back(GeometryInstanceGPU{
                .submesh_index = drawable_submesh_index,
                .node_index = static_cast<uint32_t>(node_index),
            });
        }
    }
    return result;
}

template <typename TMaterialIndexer>
void append_static_instance_buffer_data(StaticInstanceBufferData& p_destination,
    std::vector<uint32_t>& p_destination_first_instances,
    const StaticInstanceBufferData& p_source,
    const TMaterialIndexer& p_material_indexer) {
    const size_t node_offset = p_destination.nodes.size();
    const size_t submesh_offset = p_destination.submeshes.size();
    const size_t instance_offset = p_destination.instances.size();
    if (node_offset > std::numeric_limits<uint32_t>::max() || submesh_offset > std::numeric_limits<uint32_t>::max() ||
        instance_offset > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Failed to compile static-mesh resources: combined table offset exceeds uint32.");
    }
    p_destination.nodes.insert(p_destination.nodes.end(), p_source.nodes.begin(), p_source.nodes.end());
    p_destination_first_instances.reserve(p_source.first_instances.size());
    for (uint32_t first_instance : p_source.first_instances) {
        if (first_instance > std::numeric_limits<uint32_t>::max() - instance_offset) {
            throw std::runtime_error("Failed to compile static-mesh resources: combined first-instance index exceeds uint32.");
        }
        p_destination_first_instances.push_back(static_cast<uint32_t>(instance_offset) + first_instance);
    }
    for (const GeometrySubmeshGPU& source_submesh : p_source.submeshes) {
        if (source_submesh.material_id >= p_source.ordered_materials.size()) {
            throw std::runtime_error("Failed to compile static-mesh resources: source material index is out of bounds.");
        }
        p_destination.submeshes.push_back(GeometrySubmeshGPU{
            .material_id = p_material_indexer(p_destination.ordered_materials, p_source.ordered_materials[source_submesh.material_id]),
        });
    }
    for (const GeometryInstanceGPU& source_instance : p_source.instances) {
        if (source_instance.submesh_index > std::numeric_limits<uint32_t>::max() - submesh_offset ||
            source_instance.node_index > std::numeric_limits<uint32_t>::max() - node_offset) {
            throw std::runtime_error("Failed to compile static-mesh resources: combined instance index exceeds uint32.");
        }
        p_destination.instances.push_back(GeometryInstanceGPU{
            .submesh_index = static_cast<uint32_t>(submesh_offset) + source_instance.submesh_index,
            .node_index = static_cast<uint32_t>(node_offset) + source_instance.node_index,
        });
    }
}

template <typename TMaterialIndexer>
CombinedStaticInstanceBufferData build_combined_static_instance_buffer_data(const RUMesh3D& p_mesh, const TMaterialIndexer& p_material_indexer) {
    CombinedStaticInstanceBufferData result;
    append_static_instance_buffer_data(result.data,
        result.opaque_first_instances,
        build_static_instance_buffer_data(p_mesh.opaque_instances, p_material_indexer),
        p_material_indexer);
    append_static_instance_buffer_data(result.data,
        result.opaque_double_sided_first_instances,
        build_static_instance_buffer_data(p_mesh.double_sided_opaque_instances, p_material_indexer),
        p_material_indexer);
    append_static_instance_buffer_data(result.data,
        result.masked_first_instances,
        build_static_instance_buffer_data(p_mesh.masked_instances, p_material_indexer),
        p_material_indexer);
    return result;
}

Vector<MeshDrawBatchPlannerVK::Draw, RenderAllocator> make_static_draws(
    RenderAllocator* p_allocator, const RUSubmeshInstance3D& p_mesh, std::span<const uint32_t> p_first_instances) {
    if (p_mesh.submesh_count != p_first_instances.size()) {
        throw std::runtime_error("Failed to compile static-mesh resources: first-instance and submesh counts differ.");
    }
    Vector<MeshDrawBatchPlannerVK::Draw, RenderAllocator> result(p_allocator);
    result.reserve(p_mesh.submesh_count);
    for (size_t i = 0; i < p_mesh.submesh_count; ++i) {
        const RUSubmesh3D& submesh = p_mesh.submeshes.get()[i];
        if (submesh.instance_count > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to compile static-mesh resources: draw instance count exceeds uint32.");
        }
        MeshDrawBatchPlannerVK::Draw draw;
        draw.vertex_bindings = submesh.get_vertex_bindings();
        draw.indices = submesh.indices;
        draw.instance_count = static_cast<uint32_t>(submesh.instance_count);
        draw.first_instance = p_first_instances[i];
        result.push_back(std::move(draw));
    }
    return result;
}

} // namespace

MeshCompilerVK::MeshCompilerVK(RendererVK* p_renderer) : renderer(p_renderer) {
    if (renderer == nullptr || renderer->get_render_context() == nullptr || renderer->get_render_context()->allocator == nullptr) {
        throw std::runtime_error("Failed to construct mesh compiler: renderer state is missing.");
    }
}

Ref<MeshResourcesVK> MeshCompilerVK::compile(RefRes<RUMesh3D> p_mesh, const Config& p_config) const {
    if (p_mesh == MEM_NULL) {
        throw std::runtime_error("Failed to compile static-mesh resources: mesh is null.");
    }
    const bool has_opaque_draws = has_draws(p_mesh->opaque_instances);
    const bool has_opaque_double_sided_draws = has_draws(p_mesh->double_sided_opaque_instances);
    const bool has_masked_draws = has_draws(p_mesh->masked_instances);
    if (!has_opaque_draws && !has_masked_draws && !has_opaque_double_sided_draws) {
        throw std::runtime_error("Failed to compile static-mesh resources: the mesh has no drawable category.");
    }
    const auto validate_category = [](const RUSubmeshInstance3D& p_category) {
        for (size_t i = 0; i < p_category.submesh_count; ++i) {
            validate_material(p_category.submeshes.get()[i].material);
        }
    };
    validate_category(p_mesh->opaque_instances);
    validate_category(p_mesh->double_sided_opaque_instances);
    validate_category(p_mesh->masked_instances);

    const RenderContextVK* render_data = renderer->get_render_context();
    Ref<MeshResourcesVK> result = make_ref<MeshResourcesVK>(render_data->allocator, render_data);
    result->mesh = p_mesh;

    MeshBufferCompilerVK::BuildResult geometry_buffers = compile_geometry_buffers(render_data, p_mesh, p_config);
    if (geometry_buffers.vertex_buffers.empty() || geometry_buffers.index_buffer == MEM_NULL) {
        throw std::runtime_error("Failed to compile static-mesh resources: vertex or index buffers are missing.");
    }
    for (size_t i = 0; i < geometry_buffers.vertex_buffers.size(); ++i) {
        result->vertex_buffers.push_back(std::move(geometry_buffers.vertex_buffers[i]));
        result->vertex_binaries.push_back(geometry_buffers.vertex_binaries[i]);
    }
    result->indices = std::move(geometry_buffers.index_buffer);

    const auto material_indexer = [](std::vector<RefRes<RUMaterial>>& p_materials, RefRes<RUMaterial> p_material) {
        return get_or_append_material_index(p_materials, p_material);
    };
    CombinedStaticInstanceBufferData combined_data = build_combined_static_instance_buffer_data(*p_mesh, material_indexer);
    const StaticInstanceBufferData& instance_data = combined_data.data;
    result->submeshes =
        create_storage_buffer(render_data, instance_data.submeshes.data(), instance_data.submeshes.size() * sizeof(GeometrySubmeshGPU));
    result->nodes = create_storage_buffer(render_data, instance_data.nodes.data(), instance_data.nodes.size() * sizeof(GeometryNodeGPU));
    result->instances =
        create_storage_buffer(render_data, instance_data.instances.data(), instance_data.instances.size() * sizeof(GeometryInstanceGPU));

    const auto compile_category = [render_data, &result](const RUSubmeshInstance3D& p_category,
                                      std::span<const uint32_t> p_first_instances,
                                      Ref<ROBufferVK>& p_draw_buffer,
                                      Vector<MeshDrawBatchVK, RenderAllocator>& p_batches) {
        auto draws = make_static_draws(render_data->allocator, p_category, p_first_instances);
        DrawPlan plan = plan_draws(render_data, *result, draws);
        p_draw_buffer = create_indirect_buffer(render_data, plan.commands);
        p_batches = std::move(plan.batches);
    };
    if (has_opaque_draws) {
        compile_category(p_mesh->opaque_instances, combined_data.opaque_first_instances, result->opaque_draw_batches, result->opaque_batches);
    }
    if (has_opaque_double_sided_draws) {
        compile_category(p_mesh->double_sided_opaque_instances,
            combined_data.opaque_double_sided_first_instances,
            result->opaque_double_sided_draw_batches,
            result->opaque_double_sided_batches);
    }
    if (has_masked_draws) {
        compile_category(p_mesh->masked_instances, combined_data.masked_first_instances, result->masked_draw_batches, result->masked_batches);
    }

    result->materials = instance_data.ordered_materials;
    MaterialBuild material_build = compile_materials(renderer, p_config, result->materials);
    result->default_material = material_build.default_material;
    result->texture_views = std::move(material_build.texture_views);
    result->material_sampler = std::move(material_build.material_sampler);
    for (const auto& [texture, image_view] : result->texture_views) {
        static_cast<void>(texture);
        result->textures->add_resource(image_view);
    }
    if (p_mesh->resource_key != nullptr && p_mesh->resource_key[0] != '\0') {
        renderer->get_render_database()->register_compiled_render_resource(p_mesh->resource_key, result);
    }
    return result;
}

Ref<MeshResourcesVK> MeshCompilerVK::compile(RefRes<RUMesh3D> p_mesh) const {
    return compile(p_mesh, Config{});
}

Ref<MeshResourcesVK> MeshCompilerVK::acquire(std::string_view p_mesh_resource_key) const {
    return acquire(p_mesh_resource_key, Config{});
}

Ref<MeshResourcesVK> MeshCompilerVK::acquire(std::string_view p_mesh_resource_key, const Config& p_config) const {
    if (p_mesh_resource_key.empty()) {
        throw std::runtime_error("Failed to acquire compiled mesh: the mesh resource key is empty.");
    }
    Ref<MeshResourcesVK> registered = renderer->get_render_database()
                                          ->get_compiled_render_resource(p_mesh_resource_key, TypeUUID<MeshResourcesVK>::UUID)
                                          .dynamic_cast_ref<MeshResourcesVK>();
    if (registered != MEM_NULL) {
        return registered;
    }
    ResourceManager* resource_manager = ResourceManager::get_singleton();
    if (resource_manager == nullptr) {
        throw std::runtime_error(std::format("Failed to acquire compiled mesh '{}': the resource manager is unavailable.", p_mesh_resource_key));
    }
    RefRes<RUMesh3D> mesh = resource_manager->get_resource<RUMesh3D>(std::string(p_mesh_resource_key));
    if (mesh == MEM_NULL) {
        throw std::runtime_error(std::format("Failed to acquire compiled mesh '{}': no mesh resource has this key.", p_mesh_resource_key));
    }
    Ref<MeshResourcesVK> result = compile(mesh, p_config);
    if (mesh->resource_key == nullptr || std::string_view(mesh->resource_key) != p_mesh_resource_key) {
        // Register under the requested key too, so later requests find the compiled mesh.
        renderer->get_render_database()->register_compiled_render_resource(p_mesh_resource_key, result);
    }
    return result;
}

} // namespace WhiteBirdEngine

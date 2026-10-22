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
#include "function/render_vk/mesh_acceleration_structure_compiler_vk.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_acceleration_structure_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/mesh_acceleration_structure_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

struct BottomLevelInput {
    VkAccelerationStructureGeometryKHR geometry{};
    uint32_t primitive_count = 0;
    std::vector<VkTransformMatrixKHR> instance_transforms;
};

VkAccelerationStructureBuildGeometryInfoKHR make_sizing_info(
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

VkAccelerationStructureBuildSizesInfoKHR get_build_sizes(const RenderContextVK* p_render_data,
    const BaseRayTracingDataVK* p_ray_tracing_data,
    VkAccelerationStructureTypeKHR p_type,
    const VkAccelerationStructureGeometryKHR* p_geometry,
    uint32_t p_primitive_count) {
    const VkAccelerationStructureBuildGeometryInfoKHR build_info = make_sizing_info(p_type, p_geometry);
    VkAccelerationStructureBuildSizesInfoKHR result{};
    result.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    p_ray_tracing_data->get_acceleration_structure_build_sizes(
        p_render_data->logic_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &p_primitive_count, &result);
    return result;
}

VkTransformMatrixKHR to_vk_transform(const glm::mat4& p_transform) {
    VkTransformMatrixKHR result{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.matrix[row][column] = p_transform[column][row];
        }
    }
    return result;
}

const RUVertexView& get_position_binding(const RUSubmesh3D& p_submesh) {
    return p_submesh.get_vertex_bindings()[p_submesh.get_vertex_binding_index("position")];
}

const RUVertexView::Attribute& get_position_attribute(const RUVertexView& p_position_binding) {
    std::span<const RUVertexView::Attribute> attributes(p_position_binding.attributes.get(), p_position_binding.attribute_count);
    auto position_attribute = std::ranges::find_if(attributes, [](const RUVertexView::Attribute& p_attribute) {
        return p_attribute.role.get_name() == "position";
    });
    if (position_attribute == attributes.end()) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: a submesh has no position attribute.");
    }
    if (position_attribute->format != RUVertexAttributeFormat::FLOAT3) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: position attributes must use FLOAT3 format.");
    }
    return *position_attribute;
}

void validate_submesh(const RUSubmesh3D& p_submesh, size_t p_instance_count) {
    if (p_submesh.indices.index_count % 3 != 0) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: index count must form complete triangles.");
    }
    const RUVertexView& position_binding = get_position_binding(p_submesh);
    if (position_binding.attribute_count == 0 || position_binding.attributes == nullptr) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: vertex attributes are missing.");
    }
    if (position_binding.vertex_stride == 0 ||
        position_binding.binary.size != position_binding.vertex_count * position_binding.vertex_stride) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: invalid vertex range.");
    }
    if (p_submesh.indices.binary.start % sizeof(uint32_t) != 0 ||
        p_submesh.indices.binary.size != p_submesh.indices.index_count * sizeof(uint32_t)) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: invalid index range.");
    }
    if (p_submesh.first_instance > p_instance_count || p_submesh.instance_count > p_instance_count - p_submesh.first_instance) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: instance range is out of bounds.");
    }
    if (position_binding.vertex_count == 0 || position_binding.vertex_count - 1 > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: vertex count exceeds uint32.");
    }
}

void append_bottom_level_inputs(std::vector<BottomLevelInput>& p_inputs,
    const RUSubmeshInstance3D& p_mesh,
    const glm::mat4& p_mesh_transform,
    const MeshResourcesVK& p_static_mesh,
    VkDeviceAddress p_index_buffer_address) {
    if ((p_mesh.submesh_count > 0 && p_mesh.submeshes == nullptr) || (p_mesh.instance_count > 0 && p_mesh.instances == nullptr)) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: mesh storage is missing.");
    }
    for (size_t submesh_index = 0; submesh_index < p_mesh.submesh_count; ++submesh_index) {
        const RUSubmesh3D& submesh = p_mesh.submeshes.get()[submesh_index];
        if (submesh.indices.index_count == 0 || submesh.instance_count == 0) {
            // Empty geometry contributes no bottom-level structure or instances.
            continue;
        }
        validate_submesh(submesh, p_mesh.instance_count);
        const RUVertexView& position_binding = get_position_binding(submesh);
        const RUVertexView::Attribute& position_attribute = get_position_attribute(position_binding);
        const VkDeviceAddress vertex_buffer_address =
            p_static_mesh.acquire_vertex_buffer(p_static_mesh.get_vertex_buffer_index(position_binding.binary.binary))->get_device_address();
        BottomLevelInput input;
        input.geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        input.geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        input.geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& triangles = input.geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = vertex_buffer_address + position_binding.binary.start + position_attribute.offset;
        triangles.vertexStride = position_binding.vertex_stride;
        triangles.maxVertex = static_cast<uint32_t>(position_binding.vertex_count - 1);
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = p_index_buffer_address + submesh.indices.binary.start;
        input.primitive_count = static_cast<uint32_t>(submesh.indices.index_count / 3);
        input.instance_transforms.reserve(submesh.instance_count);
        for (size_t instance_index = 0; instance_index < submesh.instance_count; ++instance_index) {
            input.instance_transforms.push_back(
                to_vk_transform(p_mesh_transform * p_mesh.instances.get()[submesh.first_instance + instance_index].global_transform));
        }
        p_inputs.push_back(std::move(input));
    }
}

Ref<ROBufferVK> create_scratch_buffer(
    const RenderContextVK* p_render_data, VkDeviceSize p_size, VkDeviceSize p_alignment, VkDeviceAddress& p_aligned_address) {
    if (p_size > std::numeric_limits<VkDeviceSize>::max() - p_alignment + 1) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: invalid scratch-buffer size or alignment.");
    }
    Ref<ROBufferVK> result = p_render_data->create_buffer(ROBufferVK::Builder()
                                 .set_domain(DeviceDomain::DEVICE)
                                 .set_size(p_size + p_alignment - 1)
                                 .set_usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
    p_aligned_address = ((result->get_device_address() + p_alignment - 1) / p_alignment) * p_alignment;
    return result;
}

void require_build_input_usage(Ref<ROBufferVK> p_buffer, const char* p_message) {
    constexpr VkBufferUsageFlags REQUIRED_USAGE = MeshAccelerationStructureVK::GEOMETRY_BUFFER_USAGE;
    if (p_buffer == MEM_NULL || (p_buffer->get_usage() & REQUIRED_USAGE) != REQUIRED_USAGE) {
        throw std::runtime_error(p_message);
    }
}

} // namespace

MeshAccelerationStructureCompilerVK::MeshAccelerationStructureCompilerVK(
    const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data)
    : render_data(p_render_data), ray_tracing_data(p_ray_tracing_data) {
    if (render_data == nullptr || render_data->allocator == nullptr || ray_tracing_data == nullptr ||
        ray_tracing_data->get_acceleration_structure_build_sizes == nullptr ||
        ray_tracing_data->command_build_acceleration_structures == nullptr) {
        throw std::runtime_error("Failed to construct static-mesh acceleration structure compiler: ray-tracing state is missing.");
    }
}

Ref<MeshAccelerationStructureVK> MeshAccelerationStructureCompilerVK::compile(
    Ref<MeshResourcesVK> p_static_mesh, const glm::mat4& p_mesh_transform) const {
    if (p_static_mesh == MEM_NULL) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: static mesh is null.");
    }
    for (size_t i = 0; i < p_static_mesh->get_vertex_buffer_count(); ++i) {
        require_build_input_usage(p_static_mesh->acquire_vertex_buffer(i),
            "Failed to compile static-mesh acceleration structure: vertex buffer lacks acceleration-structure input usage.");
    }
    require_build_input_usage(p_static_mesh->acquire_indices(),
        "Failed to compile static-mesh acceleration structure: index buffer lacks acceleration-structure input usage.");
    const VkDeviceSize scratch_alignment = ray_tracing_data->min_acceleration_structure_scratch_offset_alignment;
    if (scratch_alignment == 0) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: invalid scratch-buffer alignment.");
    }

    const VkDeviceAddress index_buffer_address = p_static_mesh->acquire_indices()->get_device_address();
    std::vector<BottomLevelInput> inputs;
    RefRes<RUMesh3D> mesh = p_static_mesh->get_mesh();
    if (mesh == MEM_NULL) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: the source mesh is missing.");
    }
    append_bottom_level_inputs(inputs, mesh->opaque_instances, p_mesh_transform, *p_static_mesh, index_buffer_address);
    append_bottom_level_inputs(inputs, mesh->double_sided_opaque_instances, p_mesh_transform, *p_static_mesh, index_buffer_address);
    if (inputs.empty()) {
        throw std::runtime_error("Failed to compile static-mesh acceleration structure: the mesh has no drawable opaque geometry.");
    }

    Ref<MeshAccelerationStructureVK> result =
        make_ref<MeshAccelerationStructureVK>(render_data->allocator, render_data->allocator);
    result->command_build_acceleration_structures = ray_tracing_data->command_build_acceleration_structures;
    result->static_mesh = p_static_mesh;

    // Size one bottom-level structure per submesh; the builds share the largest scratch allocation.
    VkDeviceSize bottom_level_scratch_size = 1;
    std::vector<VkAccelerationStructureInstanceKHR> top_level_instances;
    for (const BottomLevelInput& input : inputs) {
        if (input.primitive_count > ray_tracing_data->max_acceleration_structure_primitive_count ||
            ray_tracing_data->max_acceleration_structure_geometry_count == 0) {
            throw std::runtime_error("Failed to compile static-mesh acceleration structure: device geometry limits exceeded.");
        }
        const VkAccelerationStructureBuildSizesInfoKHR sizes = get_build_sizes(
            render_data, ray_tracing_data, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &input.geometry, input.primitive_count);
        MeshAccelerationStructureVK::BottomLevelBuild build;
        build.geometry = input.geometry;
        build.build_range = VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = input.primitive_count};
        build.acceleration_structure = ROAccelerationStructureVK::Builder()
                                           .set_size(sizes.accelerationStructureSize)
                                           .set_type(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
                                           .create_ref(render_data, ray_tracing_data);
        bottom_level_scratch_size = std::max(bottom_level_scratch_size, std::max<VkDeviceSize>(sizes.buildScratchSize, 1));
        const uint64_t reference = build.acceleration_structure->get_device_address();
        for (const VkTransformMatrixKHR& transform : input.instance_transforms) {
            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = transform;
            instance.mask = 0xFF;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = reference;
            top_level_instances.push_back(instance);
        }
        result->bottom_levels.push_back(build.acceleration_structure);
        result->bottom_level_builds.push_back(std::move(build));
    }
    result->bottom_level_scratch_buffer =
        create_scratch_buffer(render_data, bottom_level_scratch_size, scratch_alignment, result->bottom_level_scratch_address);

    // Upload the instance records before sizing the top level.
    if (top_level_instances.size() > ray_tracing_data->max_acceleration_structure_primitive_count ||
        top_level_instances.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Failed to compile static-mesh top-level acceleration structure: device geometry limits exceeded.");
    }
    const size_t instance_buffer_size = sizeof(VkAccelerationStructureInstanceKHR) * top_level_instances.size();
    result->top_level_instance_buffer =
        render_data->create_buffer(ROBufferVK::Builder()
            .set_domain(DeviceDomain::HOST)
            .set_size(instance_buffer_size)
            .set_usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
    result->top_level_instance_buffer->update_buffer(top_level_instances.data(), instance_buffer_size);

    VkAccelerationStructureGeometryKHR& top_level_geometry = result->top_level_geometry;
    top_level_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    top_level_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    top_level_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    top_level_geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    top_level_geometry.geometry.instances.data.deviceAddress = result->top_level_instance_buffer->get_device_address();
    const auto top_level_instance_count = static_cast<uint32_t>(top_level_instances.size());
    const VkAccelerationStructureBuildSizesInfoKHR top_level_sizes = get_build_sizes(
        render_data, ray_tracing_data, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &top_level_geometry, top_level_instance_count);
    result->top_level = ROAccelerationStructureVK::Builder()
                            .set_size(top_level_sizes.accelerationStructureSize)
                            .set_type(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
                            .create_ref(render_data, ray_tracing_data);
    result->top_level_scratch_buffer = create_scratch_buffer(render_data,
        std::max<VkDeviceSize>(top_level_sizes.buildScratchSize, 1),
        scratch_alignment,
        result->top_level_scratch_address);
    result->top_level_build_range = VkAccelerationStructureBuildRangeInfoKHR{.primitiveCount = top_level_instance_count};
    return result;
}

} // namespace WhiteBirdEngine

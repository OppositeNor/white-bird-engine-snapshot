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
#include "function/render/render_resource_unit.hh"
#include "function/render/vertex_attribute_format_info.hh"
#include "glm/common.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_int4.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "glm/ext/vector_uint4.hpp"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

namespace {
void validate_binary_view(const RUBinaryView& p_binary_view, std::string_view p_context) {
    if (p_binary_view.binary == nullptr) {
        throw std::runtime_error(std::format("Failed to access {}: binary is missing.", p_context));
    }
    if (p_binary_view.start > p_binary_view.binary->size || p_binary_view.size > p_binary_view.binary->size - p_binary_view.start) {
        throw std::runtime_error(std::format("Failed to access {}: binary view range exceeds binary size.", p_context));
    }
    if (p_binary_view.size > 0 && p_binary_view.binary->data == nullptr) {
        throw std::runtime_error(std::format("Failed to access {}: binary data is missing.", p_context));
    }
}

// Grow [p_minimum, p_maximum] by every corner of every submesh's bounds under every instance it draws.
void expand_submesh_instance_bounds(const RUSubmeshInstance3D& p_instances, glm::vec3& p_minimum, glm::vec3& p_maximum) {
    for (size_t submesh_index = 0; submesh_index < p_instances.submesh_count; ++submesh_index) {
        const RUSubmesh3D& submesh = p_instances.submeshes.get()[submesh_index];
        if (submesh.first_instance > p_instances.instance_count ||
            submesh.instance_count > p_instances.instance_count - submesh.first_instance) {
            throw std::runtime_error(std::format("Failed to compute mesh bounds: submesh {} draws instances [{}, {}), but the category has {}.",
                submesh_index, submesh.first_instance, submesh.first_instance + submesh.instance_count, p_instances.instance_count));
        }
        const glm::vec3 half_extent = glm::vec3(submesh.aabb.width, submesh.aabb.height, submesh.aabb.depth) * 0.5F;
        for (size_t instance_index = submesh.first_instance; instance_index < submesh.first_instance + submesh.instance_count;
            ++instance_index) {
            const glm::mat4& transform = p_instances.instances.get()[instance_index].global_transform;
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const glm::vec3 sign((corner & 4U) != 0 ? 1.0F : -1.0F, (corner & 2U) != 0 ? 1.0F : -1.0F, (corner & 1U) != 0 ? 1.0F : -1.0F);
                const glm::vec3 position = glm::vec3(transform * glm::vec4(submesh.aabb.center + sign * half_extent, 1.0F));
                p_minimum = glm::min(p_minimum, position);
                p_maximum = glm::max(p_maximum, position);
            }
        }
    }
}

uint8_t* get_binary_view_data(RUBinaryView& p_binary_view, std::string_view p_context) {
    validate_binary_view(p_binary_view, p_context);
    RefRes<char> binary_data = p_binary_view.binary->data;
    return reinterpret_cast<uint8_t*>(binary_data.get()) + p_binary_view.start;
}

void validate_vertex_storage(const RUVertexView& p_vertex_data) {
    if (p_vertex_data.vertex_stride == 0) {
        if (p_vertex_data.vertex_count != 0 || p_vertex_data.binary.size != 0) {
            throw std::runtime_error("Failed to access vertex data: zero stride has non-empty vertex storage.");
        }
    } else if (p_vertex_data.vertex_count > std::numeric_limits<size_t>::max() / p_vertex_data.vertex_stride ||
               p_vertex_data.binary.size != p_vertex_data.vertex_count * p_vertex_data.vertex_stride) {
        throw std::runtime_error("Failed to access vertex data: vertex storage size does not match its count and stride.");
    }
    validate_binary_view(p_vertex_data.binary, "vertex data");
    if (p_vertex_data.attribute_count > 0 && p_vertex_data.attributes == nullptr) {
        throw std::runtime_error("Failed to access vertex data: attribute metadata storage is missing.");
    }
    size_t expected_offset = 0;
    for (size_t i = 0; i < p_vertex_data.attribute_count; ++i) {
        const RUVertexView::Attribute& attribute = p_vertex_data.attributes.get()[i];
        const std::string_view attribute_role_name = attribute.role.get_name();
        const size_t format_size = VertexAttributeFormatInfo::get_size(attribute.format);
        if (attribute.offset != expected_offset || attribute.size != format_size ||
            attribute.size > std::numeric_limits<size_t>::max() - expected_offset) {
            throw std::runtime_error("Failed to access vertex data: attributes are not tightly packed in declaration order.");
        }
        for (size_t previous_index = 0; previous_index < i; ++previous_index) {
            const RUShaderRole& previous_role = p_vertex_data.attributes.get()[previous_index].role;
            if (previous_role.get_name() == attribute_role_name) {
                throw std::runtime_error("Failed to access vertex data: attribute role is duplicated.");
            }
            if (previous_role.hash == attribute.role.hash) {
                throw std::runtime_error("Failed to access vertex data: distinct attribute roles have a hash collision.");
            }
        }
        expected_offset += attribute.size;
    }
    if (expected_offset != p_vertex_data.vertex_stride) {
        throw std::runtime_error("Failed to access vertex data: attribute sizes do not match the vertex stride.");
    }
}

const RUVertexView::Attribute& find_vertex_attribute(const RUVertexView& p_vertex_data, std::string_view p_role) {
    validate_vertex_storage(p_vertex_data);
    if (p_role.empty()) {
        throw std::runtime_error("Failed to access vertex attribute: role is empty.");
    }
    const HashCode role_hash = dynam_hash(p_role);
    const RUVertexView::Attribute* result = nullptr;
    for (size_t i = 0; i < p_vertex_data.attribute_count; ++i) {
        const RUVertexView::Attribute& attribute = p_vertex_data.attributes.get()[i];
        if (attribute.role.hash != role_hash || !attribute.role.matches(p_role)) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(std::format("Failed to access vertex attribute: role '{}' is duplicated.", p_role));
        }
        result = &attribute;
    }
    if (result == nullptr) {
        throw std::runtime_error(std::format("Failed to access vertex attribute: role '{}' does not exist.", p_role));
    }
    if (result->offset > p_vertex_data.vertex_stride || result->size > p_vertex_data.vertex_stride - result->offset) {
        throw std::runtime_error(std::format("Failed to access vertex attribute: role '{}' exceeds the vertex stride.", p_role));
    }
    return *result;
}

template <typename TValue>
void set_vertex_attribute_value(
    RUSubmesh3D& p_submesh, size_t p_vertex_index, std::string_view p_role, RUVertexAttributeFormat p_expected_format, const TValue& p_value) {
    RUVertexView& vertex_data = p_submesh.get_vertex_bindings()[p_submesh.get_vertex_binding_index(p_role)];
    if (p_vertex_index >= vertex_data.vertex_count) {
        throw std::runtime_error(std::format("Failed to set vertex attribute '{}': vertex {} is out of bounds for {} vertices.",
            p_role,
            p_vertex_index,
            vertex_data.vertex_count));
    }
    const RUVertexView::Attribute& attribute = find_vertex_attribute(vertex_data, p_role);
    if (attribute.format != p_expected_format || attribute.size != sizeof(TValue)) {
        throw std::runtime_error(
            std::format("Failed to set vertex attribute '{}': value type does not match the resource layout.", p_role));
    }
    const size_t vertex_offset = p_vertex_index * vertex_data.vertex_stride;
    if (vertex_offset > vertex_data.binary.size || attribute.offset > vertex_data.binary.size - vertex_offset ||
        sizeof(TValue) > vertex_data.binary.size - vertex_offset - attribute.offset) {
        throw std::runtime_error(std::format("Failed to set vertex attribute '{}': target storage is out of bounds.", p_role));
    }
    std::memcpy(get_binary_view_data(vertex_data.binary, "vertex data") + vertex_offset + attribute.offset, &p_value, sizeof(TValue));
}
} // namespace

std::string_view RUShaderRole::get_name() const {
    if (name == nullptr) {
        throw std::runtime_error("Failed to access shader role: role name is missing.");
    }
    const std::string_view role_name(name.get());
    if (role_name.empty()) {
        throw std::runtime_error("Failed to access shader role: role name is empty.");
    }
    if (hash != dynam_hash(role_name)) {
        throw std::runtime_error("Failed to access shader role: role hash does not match its name.");
    }
    return role_name;
}

bool RUShaderRole::matches(std::string_view p_name) const {
    if (p_name.empty() || p_name.contains('\0')) {
        throw std::runtime_error("Failed to match shader role: requested role name is invalid.");
    }
    const std::string_view retained_name = get_name();
    return hash == dynam_hash(p_name) && retained_name == p_name;
}


void RUVertexView::swap_vertices(size_t p_first, size_t p_second) {
    validate_vertex_storage(*this);
    if (p_first >= vertex_count || p_second >= vertex_count) {
        throw std::runtime_error(std::format(
            "Failed to swap vertices: indices {} and {} are out of bounds for {} vertices.", p_first, p_second, vertex_count));
    }
    if (p_first == p_second) {
        return;
    }
    uint8_t* vertex_data = get_binary_view_data(binary, "vertex data");
    uint8_t* first_vertex = vertex_data + p_first * vertex_stride;
    uint8_t* second_vertex = vertex_data + p_second * vertex_stride;
    for (size_t i = 0; i < vertex_stride; ++i) {
        std::swap(first_vertex[i], second_vertex[i]);
    }
}

size_t RUVertexView::get_vertex_attribute_offset(std::string_view p_role) const {
    return find_vertex_attribute(*this, p_role).offset;
}

void RUSubmesh3D::swap_vertices(size_t p_first, size_t p_second) {
    for (RUVertexView& binding : get_vertex_bindings()) {
        binding.swap_vertices(p_first, p_second);
    }
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, float p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::FLOAT, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::vec2& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::FLOAT2, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::vec3& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::FLOAT3, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::vec4& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::FLOAT4, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, int32_t p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::INT, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::ivec2& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::INT2, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::ivec3& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::INT3, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::ivec4& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::INT4, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, uint32_t p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::UINT, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::uvec2& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::UINT2, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::uvec3& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::UINT3, p_value);
}

void RUSubmesh3D::set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::uvec4& p_value) {
    set_vertex_attribute_value(*this, p_vertex_index, p_role, RUVertexAttributeFormat::UINT4, p_value);
}

std::span<const RUVertexView> RUSubmesh3D::get_vertex_bindings() const {
    if (vertex_binding_count > 0 && vertex_bindings == nullptr) {
        throw std::runtime_error("Failed to access vertex bindings: binding storage is missing.");
    }
    return {vertex_bindings.get(), vertex_binding_count};
}

std::span<RUVertexView> RUSubmesh3D::get_vertex_bindings() {
    if (vertex_binding_count > 0 && vertex_bindings == nullptr) {
        throw std::runtime_error("Failed to access vertex bindings: binding storage is missing.");
    }
    return {vertex_bindings.get(), vertex_binding_count};
}

size_t RUSubmesh3D::get_vertex_binding_index(std::string_view p_role) const {
    if (p_role.empty()) {
        throw std::runtime_error("Failed to access vertex attribute: role is empty.");
    }
    const std::span<const RUVertexView> bindings = get_vertex_bindings();
    const HashCode role_hash = dynam_hash(p_role);
    std::optional<size_t> result;
    for (size_t binding_index = 0; binding_index < bindings.size(); ++binding_index) {
        const RUVertexView& binding = bindings[binding_index];
        validate_vertex_storage(binding);
        for (size_t i = 0; i < binding.attribute_count; ++i) {
            const RUShaderRole& role = binding.attributes.get()[i].role;
            if (role.hash != role_hash || !role.matches(p_role)) {
                continue;
            }
            if (result.has_value()) {
                throw std::runtime_error(
                    std::format("Failed to access vertex attribute: role '{}' is declared by more than one vertex binding.", p_role));
            }
            result = binding_index;
        }
    }
    if (!result.has_value()) {
        throw std::runtime_error(std::format("Failed to access vertex attribute: role '{}' does not exist.", p_role));
    }
    return *result;
}

size_t RUSubmesh3D::get_vertex_attribute_offset(std::string_view p_role) const {
    return get_vertex_bindings()[get_vertex_binding_index(p_role)].get_vertex_attribute_offset(p_role);
}

size_t RUSubmesh3D::get_vertices_num() const {
    for (const RUVertexView& binding : get_vertex_bindings()) {
        validate_vertex_storage(binding);
        if (binding.vertex_count != vertex_count) {
            throw std::runtime_error("Failed to access vertex data: a vertex binding holds a different number of vertices.");
        }
    }
    return vertex_count;
}

RUAABB3D RUMesh3D::compute_aabb() const {
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    expand_submesh_instance_bounds(opaque_instances, minimum, maximum);
    expand_submesh_instance_bounds(masked_instances, minimum, maximum);
    expand_submesh_instance_bounds(double_sided_opaque_instances, minimum, maximum);
    if (minimum.x > maximum.x) {
        throw std::runtime_error("Failed to compute mesh bounds: the mesh draws no submesh instance.");
    }
    const glm::vec3 extent = maximum - minimum;
    return RUAABB3D{
        .center = (minimum + maximum) * 0.5F,
        .width = extent.x,
        .height = extent.y,
        .depth = extent.z,
    };
}

RUSkeleton::Joint RUSkeleton::get_joint(size_t p_index) const {
    if (p_index >= joint_count) {
        throw std::runtime_error(std::format("Failed to read skeleton joint {}: the skeleton has {} joints.", p_index, joint_count));
    }
    if (joints.binary == nullptr) {
        throw std::runtime_error("Failed to read skeleton joint: the joint binary is missing.");
    }
    if (joints.start > joints.binary->size || joints.size > joints.binary->size - joints.start) {
        throw std::runtime_error("Failed to read skeleton joint: the joint range exceeds the binary size.");
    }
    if (joints.binary->data == nullptr) {
        throw std::runtime_error("Failed to read skeleton joint: the joint binary data is missing.");
    }
    WBE_DEBUG_ASSERT(joints.size == joint_count * JOINT_BYTE_SIZE);
    const char* joint_data = joints.binary->data.get() + joints.start + (p_index * JOINT_BYTE_SIZE);
    Joint result;
    std::memcpy(&result.parent_index, joint_data, sizeof(result.parent_index));
    std::array<float, 16> bind_local_transform{};
    std::memcpy(bind_local_transform.data(), joint_data + sizeof(result.parent_index), sizeof(bind_local_transform));
    result.bind_local_transform = glm::make_mat4(bind_local_transform.data());
    if (p_index == 0 && result.parent_index != -1) {
        throw std::runtime_error("Failed to read skeleton joint 0: the root joint must have parent_index -1.");
    }
    if (p_index > 0 && result.parent_index == -1) {
        throw std::runtime_error(std::format("Failed to read skeleton joint {}: it is a second root joint.", p_index));
    }
    if (p_index > 0 && result.parent_index < 0) {
        throw std::runtime_error(
            std::format("Failed to read skeleton joint {}: it has negative parent_index {}.", p_index, result.parent_index));
    }
    if (p_index > 0 && std::cmp_greater_equal(result.parent_index, p_index)) {
        throw std::runtime_error(std::format(
            "Failed to read skeleton joint {}: its parent_index {} does not precede it.", p_index, result.parent_index));
    }
    return result;
}

} // namespace WhiteBirdEngine

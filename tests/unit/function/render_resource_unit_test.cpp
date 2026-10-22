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
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stack_allocator.hh"
#include "function/render/render_resource_unit.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

struct VertexSetterTestSpec {
    std::string_view role;
    RUVertexAttributeFormat format;
    size_t size;
};

static RefRes<char> push_render_resource_test_data(StackAllocator& p_allocator, size_t p_size, const void* p_data) {
    MemID memory = p_allocator.allocate(p_size);
    std::memcpy(p_allocator.get(memory), p_data, p_size);
    return RefRes<char>(memory, &p_allocator);
}

static RUShaderRole make_test_shader_role(StackAllocator& p_allocator, std::string_view p_name) {
    RefRes<char> retained_name = push_render_resource_test_data(p_allocator, p_name.size() + 1, p_name.data());
    retained_name.get()[p_name.size()] = '\0';
    return RUShaderRole(retained_name, dynam_hash(p_name));
}

static RUVertexView make_test_vertex_view(
    StackAllocator& p_allocator, std::span<const VertexSetterTestSpec> p_specs, size_t p_vertex_count, uint8_t p_first_fill) {
    std::vector<RUVertexView::Attribute> attributes;
    size_t stride = 0;
    for (const VertexSetterTestSpec& spec : p_specs) {
        attributes.push_back(RUVertexView::Attribute{
            .role = make_test_shader_role(p_allocator, spec.role), .format = spec.format, .offset = stride, .size = spec.size});
        stride += spec.size;
    }
    // Vertex i is filled with the byte p_first_fill + i.
    std::vector<uint8_t> vertices(stride * p_vertex_count, 0);
    for (size_t i = 0; i < p_vertex_count; ++i) {
        std::fill(vertices.begin() + static_cast<std::ptrdiff_t>(i * stride),
            vertices.begin() + static_cast<std::ptrdiff_t>((i + 1) * stride),
            static_cast<uint8_t>(p_first_fill + i));
    }

    RUBinary binary;
    binary.data = push_render_resource_test_data(p_allocator, vertices.size(), vertices.data());
    binary.size = vertices.size();

    RUVertexView result;
    result.binary.binary = push_render_resource_test_data(p_allocator, sizeof(binary), &binary).force_cast<RUBinary>();
    result.binary.size = vertices.size();
    result.attributes =
        push_render_resource_test_data(p_allocator, attributes.size() * sizeof(attributes[0]), attributes.data()).force_cast<RUVertexView::Attribute>();
    result.attribute_count = attributes.size();
    result.vertex_stride = stride;
    result.vertex_count = p_vertex_count;
    return result;
}

static RUSubmesh3D make_test_submesh(StackAllocator& p_allocator, std::span<const RUVertexView> p_bindings) {
    RUSubmesh3D result;
    result.vertex_bindings = push_render_resource_test_data(p_allocator, p_bindings.size_bytes(), p_bindings.data()).force_cast<RUVertexView>();
    result.vertex_binding_count = p_bindings.size();
    result.vertex_count = p_bindings.empty() ? 0 : p_bindings.front().vertex_count;
    return result;
}

static RUVertexView& first_binding(RUSubmesh3D& p_submesh) {
    return p_submesh.get_vertex_bindings().front();
}

static RUSubmesh3D make_vertex_setter_submesh(StackAllocator& p_allocator) {
    const std::array<VertexSetterTestSpec, 12> specs{{
        {.role = "float_value", .format = RUVertexAttributeFormat::FLOAT, .size = 4},
        {.role = "float2_value", .format = RUVertexAttributeFormat::FLOAT2, .size = 8},
        {.role = "float3_value", .format = RUVertexAttributeFormat::FLOAT3, .size = 12},
        {.role = "float4_value", .format = RUVertexAttributeFormat::FLOAT4, .size = 16},
        {.role = "int_value", .format = RUVertexAttributeFormat::INT, .size = 4},
        {.role = "int2_value", .format = RUVertexAttributeFormat::INT2, .size = 8},
        {.role = "int3_value", .format = RUVertexAttributeFormat::INT3, .size = 12},
        {.role = "int4_value", .format = RUVertexAttributeFormat::INT4, .size = 16},
        {.role = "uint_value", .format = RUVertexAttributeFormat::UINT, .size = 4},
        {.role = "uint2_value", .format = RUVertexAttributeFormat::UINT2, .size = 8},
        {.role = "uint3_value", .format = RUVertexAttributeFormat::UINT3, .size = 12},
        {.role = "uint4_value", .format = RUVertexAttributeFormat::UINT4, .size = 16},
    }};
    const std::array<RUVertexView, 1> bindings{make_test_vertex_view(p_allocator, specs, 2, 0x11)};
    return make_test_submesh(p_allocator, bindings);
}

TEST(WBERenderResourceUnitTest, ShaderRoleRetainsExactNameAndValidatesHash) {
    StackAllocator allocator(WBE_KI_B(1));
    RUShaderRole role = make_test_shader_role(allocator, "albedo");

    EXPECT_EQ(role.get_name(), "albedo");
    EXPECT_TRUE(role.matches("albedo"));
    EXPECT_FALSE(role.matches("normal"));
    EXPECT_THROW(role.matches(""), std::runtime_error);

    ++role.hash;
    EXPECT_THROW(role.get_name(), std::runtime_error);
    EXPECT_THROW(role.matches("normal"), std::runtime_error);
}

template <typename TValue>
static TValue read_vertex_setter_value(const RUSubmesh3D& p_submesh, size_t p_vertex_index, std::string_view p_role) {
    TValue result{};
    const RUVertexView& binding = p_submesh.get_vertex_bindings()[p_submesh.get_vertex_binding_index(p_role)];
    const size_t offset = p_vertex_index * binding.vertex_stride + p_submesh.get_vertex_attribute_offset(p_role);
    std::memcpy(&result, binding.binary.binary->data.get() + binding.binary.start + offset, sizeof(result));
    return result;
}

TEST(WBERenderResourceUnitTest, SwapsVerticesAndSetsEverySupportedAttributeType) {
    StackAllocator allocator(WBE_KI_B(16));
    RUSubmesh3D submesh = make_vertex_setter_submesh(allocator);
    const RUVertexView& vertices = first_binding(submesh);
    const size_t stride = vertices.vertex_stride;
    EXPECT_EQ(submesh.get_vertices_num(), 2U);
    EXPECT_EQ(submesh.get_vertex_attribute_offset("float_value"), 0U);

    submesh.swap_vertices(0, 1);
    EXPECT_EQ(vertices.binary.binary->data.get()[vertices.binary.start], 0x12);
    EXPECT_EQ(vertices.binary.binary->data.get()[vertices.binary.start + stride], 0x11);

    submesh.set_vertex_attribute(0, "float_value", 1.25F);
    submesh.set_vertex_attribute(0, "float2_value", glm::vec2(2.0F, 3.0F));
    submesh.set_vertex_attribute(0, "float3_value", glm::vec3(4.0F, 5.0F, 6.0F));
    submesh.set_vertex_attribute(0, "float4_value", glm::vec4(7.0F, 8.0F, 9.0F, 10.0F));
    submesh.set_vertex_attribute(0, "int_value", int32_t{-1});
    submesh.set_vertex_attribute(0, "int2_value", glm::ivec2(-2, -3));
    submesh.set_vertex_attribute(0, "int3_value", glm::ivec3(-4, -5, -6));
    submesh.set_vertex_attribute(0, "int4_value", glm::ivec4(-7, -8, -9, -10));
    submesh.set_vertex_attribute(0, "uint_value", uint32_t{11});
    submesh.set_vertex_attribute(0, "uint2_value", glm::uvec2(12, 13));
    submesh.set_vertex_attribute(0, "uint3_value", glm::uvec3(14, 15, 16));
    submesh.set_vertex_attribute(0, "uint4_value", glm::uvec4(17, 18, 19, 20));

    EXPECT_FLOAT_EQ(read_vertex_setter_value<float>(submesh, 0, "float_value"), 1.25F);
    EXPECT_EQ(read_vertex_setter_value<glm::vec2>(submesh, 0, "float2_value"), glm::vec2(2.0F, 3.0F));
    EXPECT_EQ(read_vertex_setter_value<glm::vec3>(submesh, 0, "float3_value"), glm::vec3(4.0F, 5.0F, 6.0F));
    EXPECT_EQ(read_vertex_setter_value<glm::vec4>(submesh, 0, "float4_value"), glm::vec4(7.0F, 8.0F, 9.0F, 10.0F));
    EXPECT_EQ(read_vertex_setter_value<int32_t>(submesh, 0, "int_value"), -1);
    EXPECT_EQ(read_vertex_setter_value<glm::ivec2>(submesh, 0, "int2_value"), glm::ivec2(-2, -3));
    EXPECT_EQ(read_vertex_setter_value<glm::ivec3>(submesh, 0, "int3_value"), glm::ivec3(-4, -5, -6));
    EXPECT_EQ(read_vertex_setter_value<glm::ivec4>(submesh, 0, "int4_value"), glm::ivec4(-7, -8, -9, -10));
    EXPECT_EQ(read_vertex_setter_value<uint32_t>(submesh, 0, "uint_value"), 11U);
    EXPECT_EQ(read_vertex_setter_value<glm::uvec2>(submesh, 0, "uint2_value"), glm::uvec2(12, 13));
    EXPECT_EQ(read_vertex_setter_value<glm::uvec3>(submesh, 0, "uint3_value"), glm::uvec3(14, 15, 16));
    EXPECT_EQ(read_vertex_setter_value<glm::uvec4>(submesh, 0, "uint4_value"), glm::uvec4(17, 18, 19, 20));
}

TEST(WBERenderResourceUnitTest, RejectsVertexBoundsRoleTypeAndLayoutFailures) {
    StackAllocator allocator(WBE_KI_B(16));
    RUSubmesh3D submesh = make_vertex_setter_submesh(allocator);

    EXPECT_THROW(submesh.swap_vertices(0, 2), std::runtime_error);
    EXPECT_THROW(submesh.set_vertex_attribute(2, "float_value", 1.0F), std::runtime_error);
    EXPECT_THROW(submesh.set_vertex_attribute(0, "missing", 1.0F), std::runtime_error);
    EXPECT_THROW(submesh.set_vertex_attribute(0, "float_value", uint32_t{1}), std::runtime_error);
    EXPECT_THROW(submesh.get_vertex_attribute_offset(""), std::runtime_error);
    EXPECT_THROW(submesh.get_vertex_attribute_offset("missing"), std::runtime_error);

    ++first_binding(submesh).binary.size;
    EXPECT_THROW(submesh.get_vertices_num(), std::runtime_error);
    --first_binding(submesh).binary.size;

    first_binding(submesh).attributes.get()[0].offset = first_binding(submesh).vertex_stride;
    EXPECT_THROW(submesh.set_vertex_attribute(0, "float_value", 1.0F), std::runtime_error);

    RUSubmesh3D mismatched_hash_submesh = make_vertex_setter_submesh(allocator);
    ++first_binding(mismatched_hash_submesh).attributes.get()[0].role.hash;
    EXPECT_THROW(mismatched_hash_submesh.get_vertices_num(), std::runtime_error);

    constexpr std::string_view FIRST_COLLISION = "role_002876ae4c05a223";
    constexpr std::string_view SECOND_COLLISION = "role_2ffa43e2b7d24f56";
    static_assert(dynam_hash(FIRST_COLLISION) == dynam_hash(SECOND_COLLISION));
    RUSubmesh3D collision_submesh = make_vertex_setter_submesh(allocator);
    first_binding(collision_submesh).attributes.get()[0].role = make_test_shader_role(allocator, FIRST_COLLISION);
    first_binding(collision_submesh).attributes.get()[1].role = make_test_shader_role(allocator, SECOND_COLLISION);
    EXPECT_THROW(collision_submesh.get_vertices_num(), std::runtime_error);

    RUSubmesh3D missing_vertex_data;
    missing_vertex_data.vertex_binding_count = 1;
    EXPECT_THROW(missing_vertex_data.get_vertex_bindings(), std::runtime_error);
    EXPECT_THROW(missing_vertex_data.get_vertex_attribute_offset("float_value"), std::runtime_error);
}

static RUSubmesh3D make_two_binding_submesh(StackAllocator& p_allocator, size_t p_second_vertex_count = 2) {
    constexpr std::array<VertexSetterTestSpec, 1> GEOMETRY_SPECS{{{.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .size = 12}}};
    constexpr std::array<VertexSetterTestSpec, 2> JOINT_SPECS{{
        {.role = "joint_binding_ids", .format = RUVertexAttributeFormat::INT4, .size = 16},
        {.role = "joint_binding_weights", .format = RUVertexAttributeFormat::FLOAT4, .size = 16},
    }};
    const std::array<RUVertexView, 2> bindings{
        make_test_vertex_view(p_allocator, GEOMETRY_SPECS, 2, 0x11),
        make_test_vertex_view(p_allocator, JOINT_SPECS, p_second_vertex_count, 0x21),
    };
    return make_test_submesh(p_allocator, bindings);
}

TEST(WBERenderResourceUnitTest, FindsAttributesAcrossVertexBindings) {
    StackAllocator allocator(WBE_KI_B(16));
    RUSubmesh3D submesh = make_two_binding_submesh(allocator);

    EXPECT_EQ(submesh.get_vertex_bindings().size(), 2U);
    EXPECT_EQ(submesh.get_vertices_num(), 2U);
    EXPECT_EQ(submesh.get_vertex_binding_index("position"), 0U);
    EXPECT_EQ(submesh.get_vertex_binding_index("joint_binding_ids"), 1U);
    EXPECT_EQ(submesh.get_vertex_binding_index("joint_binding_weights"), 1U);
    EXPECT_EQ(submesh.get_vertex_attribute_offset("joint_binding_weights"), 16U);
    EXPECT_THROW(submesh.get_vertex_binding_index("missing"), std::runtime_error);
    EXPECT_THROW(submesh.get_vertex_binding_index(""), std::runtime_error);

    submesh.set_vertex_attribute(1, "joint_binding_ids", glm::ivec4(3, 2, 1, 0));
    submesh.set_vertex_attribute(1, "position", glm::vec3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(read_vertex_setter_value<glm::ivec4>(submesh, 1, "joint_binding_ids"), glm::ivec4(3, 2, 1, 0));
    EXPECT_EQ(read_vertex_setter_value<glm::vec3>(submesh, 1, "position"), glm::vec3(1.0F, 2.0F, 3.0F));
}

TEST(WBERenderResourceUnitTest, SwapsVerticesInEveryVertexBinding) {
    StackAllocator allocator(WBE_KI_B(16));
    RUSubmesh3D submesh = make_two_binding_submesh(allocator);

    submesh.swap_vertices(0, 1);
    for (const RUVertexView& binding : submesh.get_vertex_bindings()) {
        const char* data = binding.binary.binary->data.get() + binding.binary.start;
        const uint8_t first_fill = static_cast<uint8_t>(data[binding.vertex_stride]);
        EXPECT_EQ(static_cast<uint8_t>(data[0]), first_fill + 1);
    }
}

TEST(WBERenderResourceUnitTest, RejectsInconsistentVertexBindings) {
    StackAllocator allocator(WBE_KI_B(16));
    RUSubmesh3D mismatched_count = make_two_binding_submesh(allocator, 3);
    EXPECT_THROW(mismatched_count.get_vertices_num(), std::runtime_error);

    constexpr std::array<VertexSetterTestSpec, 1> POSITION_SPECS{{{.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .size = 12}}};
    const std::array<RUVertexView, 2> duplicated_bindings{
        make_test_vertex_view(allocator, POSITION_SPECS, 2, 0x11),
        make_test_vertex_view(allocator, POSITION_SPECS, 2, 0x21),
    };
    RUSubmesh3D duplicated_role = make_test_submesh(allocator, duplicated_bindings);
    EXPECT_THROW(duplicated_role.get_vertex_binding_index("position"), std::runtime_error);
    EXPECT_THROW(duplicated_role.set_vertex_attribute(0, "position", glm::vec3(0.0F)), std::runtime_error);
}

} // namespace WhiteBirdEngine

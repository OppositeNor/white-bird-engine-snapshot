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
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_role.hh"
#include "function/render/vertex_input_requirement.hh"
#include "function/render_vk/mesh_draw_batch_planner_vk.hh"
#include "function/render_vk/mesh_draw_batch_vk.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <array>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

static RUShaderRole make_batch_role(StackAllocator& p_storage, std::string_view p_name) {
    MemID name_memory = p_storage.allocate(p_name.size() + 1);
    auto* name_data = static_cast<char*>(p_storage.get(name_memory));
    std::memcpy(name_data, p_name.data(), p_name.size());
    name_data[p_name.size()] = '\0';
    return RUShaderRole(RefRes<char>(name_memory, &p_storage), dynam_hash(p_name));
}

static RUVertexView make_batch_vertex_view(StackAllocator& p_storage,
    std::span<const std::string_view> p_roles,
    RUVertexAttributeFormat p_format,
    size_t p_attribute_size,
    size_t p_vertex_start,
    RefRes<RUBinary> p_binary = MEM_NULL) {
    MemID attributes_memory = p_storage.allocate(sizeof(RUVertexView::Attribute) * p_roles.size());
    auto* attributes = static_cast<RUVertexView::Attribute*>(p_storage.get(attributes_memory));
    for (size_t i = 0; i < p_roles.size(); ++i) {
        std::construct_at(attributes + i,
            RUVertexView::Attribute{
                .role = make_batch_role(p_storage, p_roles[i]),
                .format = p_format,
                .offset = i * p_attribute_size,
                .size = p_attribute_size,
            });
    }
    RUVertexView result;
    result.attributes = RefRes<RUVertexView::Attribute>(attributes_memory, &p_storage);
    result.attribute_count = p_roles.size();
    result.vertex_stride = p_roles.size() * p_attribute_size;
    result.vertex_count = 3;
    result.binary.binary = p_binary;
    result.binary.start = p_vertex_start;
    result.binary.size = result.vertex_stride * 3;
    return result;
}

static RUSubmesh3D make_batch_submesh_from_bindings(StackAllocator& p_storage, std::span<const RUVertexView> p_bindings, size_t p_index_start) {
    MemID bindings_memory = p_storage.allocate(p_bindings.size_bytes());
    std::memcpy(p_storage.get(bindings_memory), p_bindings.data(), p_bindings.size_bytes());
    RUSubmesh3D submesh;
    submesh.vertex_bindings = RefRes<RUVertexView>(bindings_memory, &p_storage);
    submesh.vertex_binding_count = p_bindings.size();
    submesh.vertex_count = 3;
    submesh.indices.binary.start = p_index_start;
    submesh.indices.index_count = 3;
    submesh.instance_count = 1;
    return submesh;
}

static RUSubmesh3D make_batch_submesh(
    StackAllocator& p_storage, size_t p_vertex_start, size_t p_index_start, bool p_extended = false, bool p_position_last = false) {
    constexpr std::array<std::string_view, 1> POSITION_ONLY{"position"};
    constexpr std::array<std::string_view, 2> POSITION_FIRST{"position", "normal"};
    constexpr std::array<std::string_view, 2> POSITION_LAST{"normal", "position"};
    std::span<const std::string_view> roles = POSITION_ONLY;
    if (p_extended) {
        roles = p_position_last ? std::span<const std::string_view>(POSITION_LAST) : std::span<const std::string_view>(POSITION_FIRST);
    }
    const std::array<RUVertexView, 1> bindings{make_batch_vertex_view(p_storage, roles, RUVertexAttributeFormat::FLOAT3, 12, p_vertex_start)};
    return make_batch_submesh_from_bindings(p_storage, bindings, p_index_start);
}

// A position binding of 12-byte vertices and a separate weight binding of 4-byte vertices.
static RUSubmesh3D make_two_binding_batch_submesh(StackAllocator& p_storage,
    size_t p_position_start,
    size_t p_weight_start,
    size_t p_index_start,
    RefRes<RUBinary> p_weight_binary = MEM_NULL) {
    constexpr std::array<std::string_view, 1> POSITION{"position"};
    constexpr std::array<std::string_view, 1> WEIGHT{"weight"};
    const std::array<RUVertexView, 2> bindings{
        make_batch_vertex_view(p_storage, POSITION, RUVertexAttributeFormat::FLOAT3, 12, p_position_start),
        make_batch_vertex_view(p_storage, WEIGHT, RUVertexAttributeFormat::FLOAT, 4, p_weight_start, p_weight_binary),
    };
    return make_batch_submesh_from_bindings(p_storage, bindings, p_index_start);
}

TEST(WBEMeshDrawBatchPlannerVKTest, GroupsNonAdjacentLayoutsAndPreservesInstanceAndIndexRanges) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    std::array submeshes{
        make_batch_submesh(storage, 120, 0), make_batch_submesh(storage, 240, 12, true), make_batch_submesh(storage, 12, 24)};
    submeshes[2].instance_count = 4;
    const std::array<uint32_t, 3> first_instances{7, 20, 31};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.commands.size(), 3U);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[0].first_draw, 0U);
    EXPECT_EQ(result.batches[0].draw_count, 2U);
    ASSERT_EQ(result.batches[0].vertex_buffer_offsets.size(), 1U);
    EXPECT_EQ(result.batches[0].vertex_buffer_offsets[0], 12U);
    EXPECT_EQ(result.batches[1].first_draw, 2U);
    EXPECT_EQ(result.batches[1].draw_count, 1U);
    EXPECT_EQ(result.commands[0].vertexOffset, 9);
    EXPECT_EQ(result.commands[1].vertexOffset, 0);
    EXPECT_EQ(result.commands[0].firstInstance, 7U);
    EXPECT_EQ(result.commands[1].firstInstance, 31U);
    EXPECT_EQ(result.commands[2].firstInstance, 20U);
    EXPECT_EQ(result.commands[1].instanceCount, 4U);
    EXPECT_EQ(result.commands[1].indexCount, 3U);
    EXPECT_EQ(result.commands[1].firstIndex, 6U);
    EXPECT_EQ(result.commands[2].firstIndex, 3U);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SplitsAtDeviceDrawLimit) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    std::array submeshes{make_batch_submesh(storage, 0, 0), make_batch_submesh(storage, 36, 12), make_batch_submesh(storage, 72, 24)};
    const std::array<uint32_t, 3> first_instances{0, 1, 2};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 2);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[0].draw_count, 2U);
    EXPECT_EQ(result.batches[1].first_draw, 2U);
    EXPECT_EQ(result.batches[1].draw_count, 1U);
    const auto& last = result.batches[1];
    EXPECT_EQ(last.vertex_buffer_offsets[0] + static_cast<size_t>(result.commands[2].vertexOffset) * 12, 72U);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SeparatesPipelineVariantsAndGroupsEqualVariants) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    std::array submeshes{make_batch_submesh(storage, 0, 0), make_batch_submesh(storage, 36, 12), make_batch_submesh(storage, 72, 24)};
    std::array<MeshDrawBatchPlannerVK::Draw, 3> draws;
    for (size_t i = 0; i < draws.size(); ++i) {
        draws[i].vertex_bindings = submeshes[i].get_vertex_bindings();
        draws[i].indices = submeshes[i].indices;
        draws[i].instance_count = 1;
        draws[i].first_instance = static_cast<uint32_t>(i);
    }
    draws[1].pipeline_variant = "tinted";
    auto result = MeshDrawBatchPlannerVK().build(&allocator, draws, 100);
    ASSERT_EQ(result.commands.size(), 3U);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[0].pipeline_variant, MATERIAL_PIPELINE_VARIANT_DEFAULT);
    EXPECT_EQ(result.batches[0].draw_count, 2U);
    EXPECT_EQ(result.batches[1].pipeline_variant, "tinted");
    EXPECT_EQ(result.batches[1].first_draw, 2U);
    EXPECT_EQ(result.batches[1].draw_count, 1U);
    EXPECT_EQ(result.commands[0].firstInstance, 0U);
    EXPECT_EQ(result.commands[1].firstInstance, 2U);
    EXPECT_EQ(result.commands[2].firstInstance, 1U);
}

TEST(WBEMeshDrawBatchPlannerVKTest, TakesSubmeshPipelineVariantsFromMaterials) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    constexpr std::string_view TINTED = "tinted";
    MemID variant_memory = storage.allocate(TINTED.size() + 1);
    auto* variant_data = static_cast<char*>(storage.get(variant_memory));
    std::memcpy(variant_data, TINTED.data(), TINTED.size());
    variant_data[TINTED.size()] = '\0';
    MemID material_memory = storage.allocate(sizeof(RUMaterial));
    auto* tinted_material = std::construct_at(static_cast<RUMaterial*>(storage.get(material_memory)));
    tinted_material->pipeline_variant = RefRes<char>(variant_memory, &storage);
    std::array submeshes{make_batch_submesh(storage, 0, 0), make_batch_submesh(storage, 36, 12)};
    submeshes[1].material = RefRes<RUMaterial>(material_memory, &storage);
    const std::array<uint32_t, 2> first_instances{0, 1};
    auto result = MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[0].pipeline_variant, MATERIAL_PIPELINE_VARIANT_DEFAULT);
    EXPECT_EQ(result.batches[1].pipeline_variant, TINTED);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SeparatesDifferentAttributeOffsetsAndUnalignedViewOrigins) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    std::array submeshes{make_batch_submesh(storage, 0, 0, true),
        make_batch_submesh(storage, 72, 12, true, true),
        make_batch_submesh(storage, 148, 24, true)};
    const std::array<uint32_t, 3> first_instances{0, 1, 2};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 3U);
    EXPECT_EQ(result.batches[0].vertex_bindings[0].attributes.get()[0].role.get_name(), "position");
    EXPECT_EQ(result.batches[1].vertex_bindings[0].attributes.get()[0].role.get_name(), "normal");
    EXPECT_EQ(result.batches[2].vertex_buffer_offsets[0], 148U);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SplitsBeforeSignedVertexOffsetOverflow) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    const size_t boundary = static_cast<size_t>(std::numeric_limits<int32_t>::max()) * 12;
    std::array submeshes{
        make_batch_submesh(storage, 0, 0), make_batch_submesh(storage, boundary, 12), make_batch_submesh(storage, boundary + 12, 24)};
    const std::array<uint32_t, 3> first_instances{0, 1, 2};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.commands[1].vertexOffset, std::numeric_limits<int32_t>::max());
    EXPECT_EQ(result.commands[2].vertexOffset, 0);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SkipsEmptyDrawsAndRejectsInvalidArguments) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    std::array submeshes{make_batch_submesh(storage, 0, 0), make_batch_submesh(storage, 36, 12)};
    submeshes[0].indices.index_count = 0;
    submeshes[1].instance_count = 0;
    const std::array<uint32_t, 2> first_instances{0, 0};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    EXPECT_TRUE(result.commands.empty());
    EXPECT_TRUE(result.batches.empty());
    EXPECT_THROW(MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 0),
        std::runtime_error);
    EXPECT_THROW(MeshDrawBatchPlannerVK().build(&allocator, submeshes, {}, 100),
        std::runtime_error);
    submeshes[0].indices.index_count = 3;
    submeshes[0].indices.binary.start = 1;
    EXPECT_THROW(MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100),
        std::runtime_error);
}

TEST(WBEMeshDrawBatchPlannerVKTest, BatchesBindingsWhoseFirstVerticesShiftTogether) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    // Both submeshes start 3 vertices apart in both bindings.
    std::array submeshes{make_two_binding_batch_submesh(storage, 0, 0, 0), make_two_binding_batch_submesh(storage, 36, 12, 12)};
    const std::array<uint32_t, 2> first_instances{0, 1};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 1U);
    const auto& batch = result.batches[0];
    ASSERT_EQ(batch.vertex_bindings.size(), 2U);
    EXPECT_EQ(batch.vertex_bindings[0].vertex_stride, 12U);
    EXPECT_EQ(batch.vertex_bindings[1].vertex_stride, 4U);
    EXPECT_EQ(batch.vertex_binaries.size(), 2U);
    EXPECT_EQ(batch.vertex_buffer_offsets, (std::vector<VkDeviceSize>{0, 0}));
    EXPECT_EQ(result.commands[0].vertexOffset, 0);
    EXPECT_EQ(result.commands[1].vertexOffset, 3);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SeparatesBindingsWhoseFirstVerticesDiffer) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    // The second submesh starts 3 vertices later in the position binding but 6 vertices later in the weight binding.
    std::array submeshes{make_two_binding_batch_submesh(storage, 0, 0, 0), make_two_binding_batch_submesh(storage, 36, 24, 12)};
    const std::array<uint32_t, 2> first_instances{0, 1};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[1].vertex_buffer_offsets, (std::vector<VkDeviceSize>{36, 24}));
    EXPECT_EQ(result.commands[1].vertexOffset, 0);
}

TEST(WBEMeshDrawBatchPlannerVKTest, SeparatesBindingsReadingDifferentBinaries) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    MemID first_binary = storage.allocate(sizeof(RUBinary));
    MemID second_binary = storage.allocate(sizeof(RUBinary));
    std::array submeshes{make_two_binding_batch_submesh(storage, 0, 0, 0, RefRes<RUBinary>(first_binary, &storage)),
        make_two_binding_batch_submesh(storage, 36, 12, 12, RefRes<RUBinary>(second_binary, &storage))};
    const std::array<uint32_t, 2> first_instances{0, 1};
    auto result =
        MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[0].vertex_binaries[1], RefRes<RUBinary>(first_binary, &storage));
    EXPECT_EQ(result.batches[1].vertex_binaries[1], RefRes<RUBinary>(second_binary, &storage));
}

TEST(WBEMeshDrawBatchPlannerVKTest, SeparatesDrawsWhoseUnreadBindingsDiffer) {
    StackAllocator storage(WBE_KI_B(16));
    RenderAllocator allocator(WBE_KI_B(128));
    // Planning involves no shader, so the weight bindings split the batch even for a pipeline that reads only positions.
    std::array submeshes{make_two_binding_batch_submesh(storage, 0, 0, 0), make_two_binding_batch_submesh(storage, 36, 40, 12)};
    const std::array<uint32_t, 2> first_instances{0, 1};
    auto result = MeshDrawBatchPlannerVK().build(&allocator, submeshes, first_instances, 100);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[1].vertex_buffer_offsets, (std::vector<VkDeviceSize>{36, 40}));
}

TEST(WBEMeshDrawBatchVKTest, ResolvesVertexInputAndBindsOnlyTheBindingsTheRequirementReads) {
    StackAllocator storage(WBE_KI_B(16));
    const RUSubmesh3D submesh = make_two_binding_batch_submesh(storage, 0, 0, 0);
    MeshDrawBatchVK batch;
    batch.vertex_bindings = submesh.get_vertex_bindings();
    batch.vertex_buffers = {VertexBufferBindingVK{.buffer_index = 0, .offset = 24}, VertexBufferBindingVK{.buffer_index = 1, .offset = 8}};

    VertexInputRequirement weight_only;
    weight_only.attributes.push_back(
        VertexInputRequirement::Attribute{.role = ShaderRole("weight"), .format = RUVertexAttributeFormat::FLOAT, .location = 3});
    std::vector<VertexBufferBindingVK> vertex_buffers;
    const VertexInputDescriptionVK vertex_input = batch.resolve_vertex_input(weight_only, vertex_buffers);
    ASSERT_EQ(vertex_input.bindings.size(), 1U);
    EXPECT_EQ(vertex_input.bindings[0].stride, 4U);
    EXPECT_EQ(vertex_input.source_bindings, (std::vector<size_t>{1}));
    ASSERT_EQ(vertex_buffers.size(), 1U);
    EXPECT_EQ(vertex_buffers[0].buffer_index, 1U);
    EXPECT_EQ(vertex_buffers[0].offset, 8U);

    // A vertex-less requirement binds nothing.
    EXPECT_TRUE(batch.resolve_vertex_input(VertexInputRequirement{}, vertex_buffers).bindings.empty());
    EXPECT_TRUE(vertex_buffers.empty());

    VertexInputRequirement missing;
    missing.attributes.push_back(
        VertexInputRequirement::Attribute{.role = ShaderRole("uv"), .format = RUVertexAttributeFormat::FLOAT2, .location = 0});
    EXPECT_THROW((void)batch.resolve_vertex_input(missing, vertex_buffers), std::runtime_error);
    batch.vertex_buffers.pop_back();
    EXPECT_THROW((void)batch.resolve_vertex_input(weight_only, vertex_buffers), std::runtime_error);
}

} // namespace WhiteBirdEngine

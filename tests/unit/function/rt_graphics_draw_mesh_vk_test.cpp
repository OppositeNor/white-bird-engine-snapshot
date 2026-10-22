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
#include "core/parser/parser_json.hh"
#include "function/render/mesh_node_contract.hh"
#include "function/render_vk/mesh_gpu_data_vk.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_task_registry_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_graphics_draw_mesh_vk.hh"
#include "function/render_vk/rt_params_graphics_draw_mesh_vk.hh"
#include "generated/enum_reflection.gen.hh"
#include "generated/serializables_sd.gen.hh"
#include "utils/defs.hh"
#include <cstddef>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

namespace {

RTParamsGraphicsDrawMeshVK parse_draw_mesh_params(std::string_view p_json) {
    ParserJSON parser;
    parser.parse_from_buffer(p_json);
    RTParamsGraphicsDrawMeshVK result;
    SerializableSD<RTParamsGraphicsDrawMeshVK>::deserialize(parser.get_data(), result);
    return result;
}

std::string create_error(const RTParamsGraphicsDrawMeshVK& p_params, const RenderContextVK* p_render_data) {
    try {
        (void)RTGraphicsDrawMeshVK::create(p_render_data, p_params, RendGraphContextVK{.swap_chain_extent = {.width = 64, .height = 64}});
    } catch (const std::runtime_error& p_error) {
        return p_error.what();
    }
    return {};
}

} // namespace

TEST(WBERTGraphicsDrawMeshVKTest, IsRegisteredUnderItsNameAndAlias) {
    EXPECT_TRUE(RenderTaskRegistryVK::contains("RTGraphicsDrawMeshVK"));
    EXPECT_TRUE(RenderTaskRegistryVK::contains("graphics_draw_mesh"));
}

TEST(WBERTGraphicsDrawMeshVKTest, ParametersNamePartsByAlias) {
    const RTParamsGraphicsDrawMeshVK params = parse_draw_mesh_params(
        R"({"mesh": "sponza", "pipeline": "gbuffer", "parts": ["opaque", "DOUBLE_SIDED", "masked"], "transform": "sponza_transform", "depth_load_op": "load"})");
    EXPECT_EQ(params.mesh, "sponza");
    EXPECT_EQ(params.parts, (std::vector<MeshPartVK>{MeshPartVK::OPAQUE, MeshPartVK::DOUBLE_SIDED, MeshPartVK::MASKED}));
    EXPECT_EQ(params.transform, std::optional<std::string>("sponza_transform"));
    EXPECT_EQ(params.depth_load_op, AttachmentLoadOpVK::LOAD);
    EXPECT_EQ(params.color_load_op, AttachmentLoadOpVK::CLEAR);
    EXPECT_THROW((void)parse_draw_mesh_params(R"({"mesh": "m", "pipeline": "p", "parts": ["translucent"]})"), std::runtime_error);
    EXPECT_THROW((void)parse_draw_mesh_params(R"({"mesh": "m", "pipeline": "p"})"), std::runtime_error);
    EXPECT_THROW((void)parse_draw_mesh_params(R"({"mesh": "m", "pipeline": "p", "parts": ["opaque"], "part": "masked"})"), std::runtime_error);
}

TEST(WBERTGraphicsDrawMeshVKTest, CreateValidatesPartsBeforeLookingUpResources) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    RTParamsGraphicsDrawMeshVK params = parse_draw_mesh_params(R"({"mesh": "m", "pipeline": "p", "parts": []})");
    EXPECT_NE(create_error(params, &render_data).find("at least one mesh part"), std::string::npos);
    params.parts = {MeshPartVK::OPAQUE, MeshPartVK::MASKED, MeshPartVK::OPAQUE};
    EXPECT_NE(create_error(params, &render_data).find("'OPAQUE' is listed more than once"), std::string::npos);
    params.parts = {MeshPartVK::MASKED};
    EXPECT_NE(create_error(params, &render_data).find("render database is unavailable"), std::string::npos);
}

TEST(WBEMeshResourcesVKTest, NamesPartsAndMaterialTables) {
    EXPECT_EQ(EnumReflection<MeshPartVK>::string_to_enum("double_sided"), MeshPartVK::DOUBLE_SIDED);
    EXPECT_EQ(MeshResourcesVK::get_draw_commands_member(MeshPartVK::OPAQUE), MeshResourcesVK::OPAQUE_DRAW_BATCHES);
    EXPECT_EQ(MeshResourcesVK::get_draw_commands_member(MeshPartVK::MASKED), MeshResourcesVK::MASKED_DRAW_BATCHES);
    EXPECT_EQ(MeshResourcesVK::get_draw_commands_member(MeshPartVK::DOUBLE_SIDED), MeshResourcesVK::OPAQUE_DOUBLE_SIDED_DRAW_BATCHES);
    EXPECT_EQ(MeshResourcesVK::get_material_params_member(2), "material_params.2");

    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    const MeshResourcesVK resources(&render_data);
    for (MeshPartVK part : {MeshPartVK::OPAQUE, MeshPartVK::MASKED, MeshPartVK::DOUBLE_SIDED}) {
        EXPECT_EQ(resources.acquire_draw_commands(part), MEM_NULL);
        EXPECT_EQ(resources.get_draw_batch_count(part), 0U);
        EXPECT_THROW((void)resources.get_draw_batch(part, 0), std::runtime_error);
    }
    EXPECT_EQ(resources.get_material_table_count(), 0U);
    EXPECT_THROW((void)resources.acquire_material_params(0), std::runtime_error);
    EXPECT_NE(resources.get_type_uuid(), 0U);
}

TEST(WBEMeshResourcesVKTest, ValidatesPipelineNodeContracts) {
    MeshNodeContract contract;
    contract.element_stride = sizeof(GeometryNodeGPU);
    contract.transform_offset = offsetof(GeometryNodeGPU, transform);
    contract.normal_transform_offset = offsetof(GeometryNodeGPU, normal_transform);
    EXPECT_NO_THROW(MeshResourcesVK::validate_node_contract(contract));
    contract.element_stride += 16;
    EXPECT_THROW(MeshResourcesVK::validate_node_contract(contract), std::runtime_error);
}

} // namespace WhiteBirdEngine

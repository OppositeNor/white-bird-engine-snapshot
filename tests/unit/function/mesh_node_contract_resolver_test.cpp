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
#include "function/render/shader_metadata.hh"
#include "function/render/mesh_node_contract.hh"
#include "function/render/mesh_node_contract_resolver.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {
namespace {

ShaderMetadataTypeField make_node_field(std::string_view p_name, int32_t p_offset, int32_t p_size) {
    ShaderMetadataTypeField result;
    result.name = p_name;
    result.binding.kind = "uniform";
    result.binding.offset = p_offset;
    result.binding.size = p_size;
    return result;
}

ShaderMetadata make_mesh_node_metadata(bool p_include_normal_transform = true) {
    ShaderMetadata metadata;
    ShaderMetadataParameter mesh_parameter;
    mesh_parameter.name = "mesh";
    mesh_parameter.type.kind = "parameterBlock";

    ShaderMetadataTypeField nodes_field;
    nodes_field.name = "nodes";
    nodes_field.type.kind = "resource";
    nodes_field.type.scalar_type = "structuredBuffer";
    nodes_field.fields.push_back(make_node_field("transform", 0, 64));
    if (p_include_normal_transform) {
        nodes_field.fields.push_back(make_node_field("normal_transform", 64, 64));
    }
    mesh_parameter.type.fields.push_back(std::move(nodes_field));
    metadata.parameters.push_back(std::move(mesh_parameter));
    return metadata;
}

} // namespace

TEST(WBEMeshNodeContractResolverTest, ResolvesNodeStrideAndFieldOffsets) {
    const MeshNodeContract contract = MeshNodeContractResolver().resolve(make_mesh_node_metadata(), "mesh");

    EXPECT_EQ(contract.element_stride, 128U);
    EXPECT_EQ(contract.transform_offset, 0U);
    EXPECT_EQ(contract.normal_transform_offset, 64U);
}

TEST(WBEMeshNodeContractResolverTest, RejectsMissingNormalTransformField) {
    EXPECT_THROW(MeshNodeContractResolver().resolve(make_mesh_node_metadata(false), "mesh"), std::runtime_error);
}

} // namespace WhiteBirdEngine
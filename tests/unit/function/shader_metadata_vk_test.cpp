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
#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "function/render/mesh_node_contract.hh"
#include "function/render/mesh_node_contract_resolver.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_query.hh"
#include "platform/file_system/path.hh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

TEST(WBEShaderMetadataVKTest, DeserializesNormalizedSlangUserAttributes) {
    std::string json_buffer = R"(
    {
        "parameters": [
            {
                "name": "material_texture",
                "user_attributes": [
                    {
                        "name": "MaterialSlot",
                        "arguments": [
                            {"kind": "string", "string_value": "albedo"},
                            {"kind": "int", "integer_value": 7},
                            {"kind": "float", "floating_point_value": 0.5}
                        ]
                    }
                ],
                "type": {
                    "kind": "struct",
                    "user_attributes": [{"name": "MaterialContract", "arguments": []}],
                    "fields": [
                        {
                            "name": "texture",
                            "type": {
                                "kind": "resource",
                                "scalar_type": "texture2D",
                                "access": "readWrite",
                                "user_attributes": [{"name": "TextureShape", "arguments": []}]
                            },
                            "user_attributes": [{"name": "Required", "arguments": []}]
                        }
                    ]
                }
            }
        ],
        "entry_points": [
            {
                "name": "fragment_main",
                "stage": "fragment",
                "user_attributes": [{"name": "MaterialShader", "arguments": []}]
            }
        ]
    }
    )";
    ParserJSON parser;
    parser.parse_from_buffer(json_buffer);
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    ASSERT_EQ(metadata.parameters.size(), 1U);
    const ShaderMetadataParameter& parameter = metadata.parameters[0];
    ASSERT_EQ(parameter.user_attributes.size(), 1U);
    const ShaderMetadataUserAttribute& attribute = parameter.user_attributes[0];
    EXPECT_EQ(attribute.name, "MaterialSlot");
    ASSERT_EQ(attribute.arguments.size(), 3U);
    EXPECT_EQ(attribute.arguments[0].kind, "string");
    EXPECT_EQ(attribute.arguments[0].string_value, "albedo");
    EXPECT_EQ(attribute.arguments[1].kind, "int");
    EXPECT_EQ(attribute.arguments[1].integer_value, 7);
    EXPECT_EQ(attribute.arguments[2].kind, "float");
    EXPECT_DOUBLE_EQ(attribute.arguments[2].floating_point_value, 0.5);

    ASSERT_EQ(parameter.type.user_attributes.size(), 1U);
    EXPECT_EQ(parameter.type.user_attributes[0].name, "MaterialContract");
    ASSERT_EQ(parameter.type.fields.size(), 1U);
    ASSERT_EQ(parameter.type.fields[0].user_attributes.size(), 1U);
    EXPECT_EQ(parameter.type.fields[0].user_attributes[0].name, "Required");
    ASSERT_EQ(parameter.type.fields[0].type.user_attributes.size(), 1U);
    EXPECT_EQ(parameter.type.fields[0].type.user_attributes[0].name, "TextureShape");
    EXPECT_EQ(parameter.type.fields[0].type.access, "readWrite");

    ASSERT_EQ(metadata.entry_points.size(), 1U);
    ASSERT_EQ(metadata.entry_points[0].user_attributes.size(), 1U);
    EXPECT_EQ(metadata.entry_points[0].user_attributes[0].name, "MaterialShader");
}

TEST(WBEShaderMetadataVKTest, MissingUserAttributesRemainEmpty) {
    std::string json_buffer = R"(
    {
        "parameters": [{"name": "camera_info", "type": {"fields": [{"name": "view", "type": {}}]}}],
        "entry_points": [{"name": "vertex_main", "stage": "vertex"}]
    }
    )";
    ParserJSON parser;
    parser.parse_from_buffer(json_buffer);
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    ASSERT_EQ(metadata.parameters.size(), 1U);
    EXPECT_TRUE(metadata.parameters[0].user_attributes.empty());
    EXPECT_TRUE(metadata.parameters[0].type.user_attributes.empty());
    ASSERT_EQ(metadata.parameters[0].type.fields.size(), 1U);
    EXPECT_TRUE(metadata.parameters[0].type.fields[0].user_attributes.empty());
    EXPECT_TRUE(metadata.parameters[0].type.fields[0].type.user_attributes.empty());
    ASSERT_EQ(metadata.entry_points.size(), 1U);
    EXPECT_TRUE(metadata.entry_points[0].user_attributes.empty());
}

TEST(WBEShaderMetadataVKTest, PreservesWrappedTypeAttributesAndNames) {
    std::string json_buffer = R"(
    {
        "parameters": [
            {
                "name": "mesh",
                "type": {
                    "kind": "parameterBlock",
                    "name": "MeshContractMetadata",
                    "user_attributes": [{"name": "StructContract", "arguments": []}],
                    "fields": [
                        {
                            "name": "nodes",
                            "type": {
                                "kind": "resource",
                                "name": "NodeData",
                                "scalar_type": "structuredBuffer",
                                "element_scalar_type": "struct",
                                "user_attributes": [
                                    {"name": "StructContract", "arguments": [{"kind": "string", "string_value": "custom_node"}]}
                                ]
                            },
                            "fields": [
                                {
                                    "name": "transform",
                                    "type": {"kind": "matrix", "scalar_type": "matrix"}
                                }
                            ]
                        }
                    ]
                }
            }
        ],
        "entry_points": []
    }
    )";
    ParserJSON parser;
    parser.parse_from_buffer(json_buffer);
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    ASSERT_EQ(metadata.parameters.size(), 1U);
    const ShaderMetadataParameter& parameter = metadata.parameters[0];
    EXPECT_EQ(parameter.type.kind, "parameterBlock");
    EXPECT_EQ(parameter.type.name, "MeshContractMetadata");
    ASSERT_EQ(parameter.type.user_attributes.size(), 1U);
    EXPECT_EQ(parameter.type.user_attributes[0].name, "StructContract");
    EXPECT_TRUE(parameter.type.user_attributes[0].arguments.empty());

    ASSERT_EQ(parameter.type.fields.size(), 1U);
    const ShaderMetadataTypeField& nodes_field = parameter.type.fields[0];
    EXPECT_EQ(nodes_field.type.kind, "resource");
    EXPECT_EQ(nodes_field.type.name, "NodeData");
    ASSERT_EQ(nodes_field.type.user_attributes.size(), 1U);
    EXPECT_EQ(nodes_field.type.user_attributes[0].name, "StructContract");
    ASSERT_EQ(nodes_field.type.user_attributes[0].arguments.size(), 1U);
    EXPECT_EQ(nodes_field.type.user_attributes[0].arguments[0].kind, "string");
    EXPECT_EQ(nodes_field.type.user_attributes[0].arguments[0].string_value, "custom_node");
}

TEST(WBEShaderMetadataVKTest, LoadsUserAttributesFromCompiledSlangMetadata) {
    ParserJSON parser;
    parser.parse((Path("test_env/res/assets/shaders/reflection_attributes") / "reflection_attributes.json"));
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    ASSERT_EQ(metadata.parameters.size(), 1U);
    const ShaderMetadataParameter& global_parameter = metadata.parameters[0];
    EXPECT_EQ(global_parameter.name, "attributed_global");
    ASSERT_EQ(global_parameter.user_attributes.size(), 6U);
    EXPECT_EQ(global_parameter.user_attributes[0].name, "VariableContract");
    ASSERT_EQ(global_parameter.user_attributes[0].arguments.size(), 1U);
    EXPECT_EQ(global_parameter.user_attributes[0].arguments[0].kind, "string");
    EXPECT_EQ(global_parameter.user_attributes[0].arguments[0].string_value, "global");
    EXPECT_EQ(global_parameter.user_attributes[1].name, "BindingContract");
    ASSERT_EQ(global_parameter.user_attributes[1].arguments.size(), 1U);
    EXPECT_EQ(global_parameter.user_attributes[1].arguments[0].kind, "int");
    EXPECT_EQ(global_parameter.user_attributes[1].arguments[0].integer_value, 3);
    EXPECT_EQ(global_parameter.user_attributes[2].name, "MarkerContract");
    EXPECT_TRUE(global_parameter.user_attributes[2].arguments.empty());
    EXPECT_EQ(global_parameter.user_attributes[3].name, "RangeContract");
    ASSERT_EQ(global_parameter.user_attributes[3].arguments.size(), 2U);
    EXPECT_EQ(global_parameter.user_attributes[3].arguments[0].kind, "int");
    EXPECT_EQ(global_parameter.user_attributes[3].arguments[0].integer_value, -4);
    EXPECT_EQ(global_parameter.user_attributes[3].arguments[1].kind, "int");
    EXPECT_EQ(global_parameter.user_attributes[3].arguments[1].integer_value, 9);
    EXPECT_EQ(global_parameter.user_attributes[4].name, "WeightContract");
    ASSERT_EQ(global_parameter.user_attributes[4].arguments.size(), 1U);
    EXPECT_EQ(global_parameter.user_attributes[4].arguments[0].kind, "float");
    EXPECT_DOUBLE_EQ(global_parameter.user_attributes[4].arguments[0].floating_point_value, -0.25);
    EXPECT_EQ(global_parameter.user_attributes[5].name, "AliasContract");
    ASSERT_EQ(global_parameter.user_attributes[5].arguments.size(), 1U);
    EXPECT_EQ(global_parameter.user_attributes[5].arguments[0].kind, "string");
    EXPECT_TRUE(global_parameter.user_attributes[5].arguments[0].string_value.empty());

    ASSERT_EQ(global_parameter.type.element_type.user_attributes.size(), 1U);
    const ShaderMetadataUserAttribute& struct_attribute = global_parameter.type.element_type.user_attributes[0];
    EXPECT_EQ(struct_attribute.name, "StructContract");
    ASSERT_EQ(struct_attribute.arguments.size(), 1U);
    EXPECT_EQ(struct_attribute.arguments[0].kind, "float");
    EXPECT_DOUBLE_EQ(struct_attribute.arguments[0].floating_point_value, 0.5);

    ASSERT_EQ(global_parameter.type.fields.size(), 7U);
    ASSERT_EQ(global_parameter.type.fields[0].user_attributes.size(), 1U);
    EXPECT_EQ(global_parameter.type.fields[0].user_attributes[0].name, "VariableContract");
    ASSERT_EQ(global_parameter.type.fields[0].user_attributes[0].arguments.size(), 1U);
    EXPECT_EQ(global_parameter.type.fields[0].user_attributes[0].arguments[0].string_value, "field");
    for (size_t i = 1; i < global_parameter.type.fields.size(); ++i) {
        const ShaderMetadataTypeField& texture_field = global_parameter.type.fields[i];
        ASSERT_EQ(texture_field.user_attributes.size(), 1U);
        EXPECT_EQ(texture_field.user_attributes[0].name, "WBE_TEXTURE");
        ASSERT_EQ(texture_field.user_attributes[0].arguments.size(), 1U);
        EXPECT_EQ(texture_field.user_attributes[0].arguments[0].kind, "string");
        std::string expected_texture_role = i < 6 ? std::format("role_{}", i) : std::string();
        EXPECT_EQ(texture_field.user_attributes[0].arguments[0].string_value, expected_texture_role);
    }

    ASSERT_EQ(metadata.entry_points.size(), 1U);
    const ShaderMetadataEntryPoint& entry_point = metadata.entry_points[0];
    EXPECT_EQ(entry_point.name, "fragment_main");
    ASSERT_EQ(entry_point.user_attributes.size(), 1U);
    EXPECT_EQ(entry_point.user_attributes[0].name, "EntryContract");
    ASSERT_EQ(entry_point.user_attributes[0].arguments.size(), 1U);
    EXPECT_EQ(entry_point.user_attributes[0].arguments[0].string_value, "entry");

    ASSERT_EQ(entry_point.parameters.size(), 1U);
    ASSERT_EQ(entry_point.parameters[0].user_attributes.size(), 1U);
    EXPECT_EQ(entry_point.parameters[0].user_attributes[0].name, "ParameterContract");
    ASSERT_EQ(entry_point.parameters[0].user_attributes[0].arguments.size(), 1U);
    EXPECT_EQ(entry_point.parameters[0].user_attributes[0].arguments[0].kind, "int");
    EXPECT_EQ(entry_point.parameters[0].user_attributes[0].arguments[0].integer_value, 7);
}

TEST(WBEShaderMetadataVKTest, LoadsExplicitDescriptorHeapIndicesFromCompiledSlangMetadata) {
    ParserJSON parser;
    parser.parse((Path("test_env/res/assets/shaders/descriptor_heap_reflection") / "descriptor_heap_reflection.json"));
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    const ShaderMetadataQuery query;
    const ShaderMetadataParameter* acceleration_structure = query.find_parameter(metadata, "acceleration_structure");
    ASSERT_NE(acceleration_structure, nullptr);
    EXPECT_EQ(acceleration_structure->type.kind, "resource");
    EXPECT_EQ(acceleration_structure->type.scalar_type, "accelerationStructure");

    const ShaderMetadataParameter* push_data_pointer = query.find_parameter(metadata, "descriptor_indices");
    ASSERT_NE(push_data_pointer, nullptr);
    const ShaderMetadataParameter& push_data = *push_data_pointer;
    EXPECT_EQ(push_data.name, "descriptor_indices");
    EXPECT_EQ(push_data.binding.kind, "pushConstantBuffer");
    EXPECT_EQ(push_data.type.kind, "constantBuffer");
    ASSERT_EQ(push_data.type.fields.size(), 7U);

    const ShaderMetadataTypeField& uniform_buffer = push_data.type.fields[0];
    EXPECT_EQ(uniform_buffer.name, "uniform_buffer");
    EXPECT_EQ(uniform_buffer.type.kind, "vector");
    EXPECT_EQ(uniform_buffer.type.element_scalar_type, "uint32");
    EXPECT_EQ(uniform_buffer.type.element_count, 2);
    EXPECT_EQ(uniform_buffer.binding.offset, 0);
    EXPECT_EQ(uniform_buffer.binding.size, 8);
    ASSERT_EQ(uniform_buffer.user_attributes.size(), 1U);
    EXPECT_EQ(uniform_buffer.user_attributes[0].name, "WBE_DESCRIPTOR");
    // Slang reflects the default value of the omitted access argument.
    ASSERT_EQ(uniform_buffer.user_attributes[0].arguments.size(), 3U);
    EXPECT_EQ(uniform_buffer.user_attributes[0].arguments[0].string_value, "uniform_buffer");
    EXPECT_EQ(uniform_buffer.user_attributes[0].arguments[1].string_value, "uniform_buffer");
    EXPECT_EQ(uniform_buffer.user_attributes[0].arguments[2].string_value, "");

    const ShaderMetadataTypeField& images = push_data.type.fields[1];
    EXPECT_EQ(images.name, "images");
    EXPECT_EQ(images.binding.offset, 8);
    EXPECT_EQ(images.binding.size, 16);
    ASSERT_EQ(images.fields.size(), 2U);
    EXPECT_EQ(images.fields[0].name, "sampled_image");
    EXPECT_EQ(images.fields[0].binding.offset, 0);
    EXPECT_EQ(images.fields[0].user_attributes[0].arguments[0].string_value, "sampled_image");
    EXPECT_EQ(images.fields[1].name, "storage_image");
    EXPECT_EQ(images.fields[1].binding.offset, 8);
    EXPECT_EQ(images.fields[1].user_attributes[0].arguments[0].string_value, "storage_image");

    EXPECT_EQ(push_data.type.fields[2].binding.offset, 24);
    EXPECT_EQ(push_data.type.fields[2].user_attributes[0].arguments[0].string_value, "storage_buffer");
    EXPECT_EQ(push_data.type.fields[3].binding.offset, 32);
    EXPECT_EQ(push_data.type.fields[3].user_attributes[0].arguments[0].string_value, "sampler");
    EXPECT_EQ(push_data.type.fields[4].name, "scalar_uniform");
    EXPECT_EQ(push_data.type.fields[4].binding.offset, 40);
    EXPECT_EQ(push_data.type.fields[4].user_attributes[0].arguments[0].string_value, "uniform_buffer");

    const ShaderMetadataTypeField& acceleration_structure_index = push_data.type.fields[5];
    EXPECT_EQ(acceleration_structure_index.name, "acceleration_structure");
    EXPECT_EQ(acceleration_structure_index.type.kind, "vector");
    EXPECT_EQ(acceleration_structure_index.type.element_scalar_type, "uint32");
    EXPECT_EQ(acceleration_structure_index.type.element_count, 2);
    EXPECT_EQ(acceleration_structure_index.binding.offset, 48);
    EXPECT_EQ(acceleration_structure_index.binding.size, 8);
    ASSERT_EQ(acceleration_structure_index.user_attributes.size(), 1U);
    EXPECT_EQ(acceleration_structure_index.user_attributes[0].name, "WBE_DESCRIPTOR");
    ASSERT_EQ(acceleration_structure_index.user_attributes[0].arguments.size(), 3U);
    EXPECT_EQ(acceleration_structure_index.user_attributes[0].arguments[0].string_value, "acceleration_structure");
    EXPECT_EQ(acceleration_structure_index.user_attributes[0].arguments[1].string_value, "acceleration_structure");

    const ShaderMetadataTypeField& time = push_data.type.fields[6];
    EXPECT_EQ(time.name, "time");
    EXPECT_EQ(time.type.kind, "scalar");
    EXPECT_EQ(time.type.scalar_type, "float32");
    EXPECT_EQ(time.binding.offset, 56);
    EXPECT_EQ(time.binding.size, 4);
    ASSERT_EQ(time.user_attributes.size(), 1U);
    EXPECT_EQ(time.user_attributes[0].name, "WBE_PUSH_CONSTANT");
    ASSERT_EQ(time.user_attributes[0].arguments.size(), 1U);
    EXPECT_EQ(time.user_attributes[0].arguments[0].string_value, "time");
}

TEST(WBEShaderMetadataVKTest, LoadsNestedMaterialTextureContractFromCompiledSlangMetadata) {
    ParserJSON parser;
    parser.parse((Path("test_env/res/assets/shaders/forward_cook_torrance_pbr") / "forward_cook_torrance_pbr.json"));
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    auto material_params = std::ranges::find_if(metadata.parameters, [](const ShaderMetadataParameter& p_parameter) {
        return p_parameter.name == "material_params";
    });
    ASSERT_NE(material_params, metadata.parameters.end());
    ASSERT_EQ(material_params->type.fields.size(), 1U);
    const ShaderMetadataTypeField& materials = material_params->type.fields[0];
    EXPECT_EQ(materials.name, "materials");
    EXPECT_EQ(materials.type.kind, "array");
    EXPECT_EQ(materials.type.element_count, 32);
    EXPECT_EQ(materials.binding.element_stride, 16);
    ASSERT_EQ(materials.fields.size(), 3U);

    const std::array<std::string_view, 3> expected_names{"albedo_index", "normal_index", "rma_index"};
    const std::array<std::string_view, 3> expected_texture_roles{"albedo", "normal", "rma"};
    for (size_t i = 0; i < materials.fields.size(); ++i) {
        const ShaderMetadataTypeField& field = materials.fields[i];
        EXPECT_EQ(field.name, expected_names[i]);
        EXPECT_EQ(field.type.scalar_type, "int32");
        EXPECT_EQ(field.binding.offset, static_cast<int32_t>(i * sizeof(int32_t)));
        ASSERT_EQ(field.user_attributes.size(), 1U);
        EXPECT_EQ(field.user_attributes[0].name, "WBE_TEXTURE");
        ASSERT_EQ(field.user_attributes[0].arguments.size(), 1U);
        EXPECT_EQ(field.user_attributes[0].arguments[0].kind, "string");
        EXPECT_EQ(field.user_attributes[0].arguments[0].string_value, expected_texture_roles[i]);
    }
}

TEST(WBEShaderMetadataVKTest, LoadsMeshStorageBuffersFromCompiledSlangMetadata) {
    ParserJSON parser;
    parser.parse((Path("test_env/res/assets/shaders/forward_cook_torrance_pbr") / "forward_cook_torrance_pbr.json"));
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    auto parameter = std::ranges::find_if(metadata.parameters, [](const ShaderMetadataParameter& p_parameter) {
        return p_parameter.name == "descriptor_indices";
    });
    ASSERT_NE(parameter, metadata.parameters.end());
    EXPECT_EQ(parameter->type.kind, "constantBuffer");
    EXPECT_EQ(parameter->binding.kind, "pushConstantBuffer");

    auto mesh = std::ranges::find_if(parameter->type.fields, [](const ShaderMetadataTypeField& p_field) {
        return p_field.name == "mesh";
    });
    ASSERT_NE(mesh, parameter->type.fields.end());
    EXPECT_EQ(mesh->type.kind, "struct");

    // Static-mesh shader: the mesh transform is only declared by WBE_MESH_TRANSFORM variants.
    const std::array<std::string_view, 3> expected_names{"submeshes", "nodes", "instances"};
    const std::array<std::string_view, 3> expected_resource_ids{"mesh_submeshes", "mesh_nodes", "mesh_instances"};
    ASSERT_EQ(mesh->fields.size(), expected_names.size());
    for (size_t i = 0; i < expected_names.size(); ++i) {
        const ShaderMetadataTypeField& field = mesh->fields[i];
        EXPECT_EQ(field.name, expected_names[i]);
        EXPECT_EQ(field.type.kind, "vector");
        EXPECT_EQ(field.type.element_scalar_type, "uint32");
        EXPECT_EQ(field.type.element_count, 2);
        EXPECT_EQ(field.binding.kind, "uniform");
        EXPECT_EQ(field.binding.offset, static_cast<int32_t>(i * sizeof(uint32_t) * 2));
        ASSERT_EQ(field.user_attributes.size(), 1U);
        EXPECT_EQ(field.user_attributes[0].name, "WBE_DESCRIPTOR");
        ASSERT_EQ(field.user_attributes[0].arguments.size(), 3U);
        EXPECT_EQ(field.user_attributes[0].arguments[0].string_value, "storage_buffer");
        EXPECT_EQ(field.user_attributes[0].arguments[1].string_value, expected_resource_ids[i]);
        EXPECT_EQ(field.user_attributes[0].arguments[2].string_value, "read");
    }
}

TEST(WBEShaderMetadataVKTest, LoadsMeshNodeContractFromCompiledSlangMetadata) {
    ParserJSON parser;
    parser.parse((Path("test_env/res/assets/shaders/forward_cook_torrance_pbr") / "forward_cook_torrance_pbr.json"));
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    const MeshNodeContract contract = MeshNodeContractResolver().resolve(metadata, "mesh");
    EXPECT_EQ(contract.element_stride, 128U);
    EXPECT_EQ(contract.transform_offset, 0U);
    EXPECT_EQ(contract.normal_transform_offset, 64U);
}

namespace {

void expect_deferred_masked_pipeline_resource(const Path& p_directory) {
    ParserJSON parser;
    parser.parse((p_directory / "deferred_gbuffer_shader.json"));

    std::vector<DataValue> resources = parser.get_value<std::vector<DataValue>>("resources");
    auto resource = std::ranges::find_if(resources, [](const DataValue& p_resource) {
        return p_resource.contains("id") && p_resource.get_value<std::string>("id") == "deferred_gbuffer_masked";
    });
    ASSERT_NE(resource, resources.end());
    EXPECT_EQ(resource->get_value<std::string>("type"), "graphics_pipeline");
    EXPECT_EQ(resource->get_value<std::string>("vertex_stage_id"), "deferred_gbuffer_shader");
    EXPECT_FALSE(resource->contains("vertex_stage_entry"));
    EXPECT_EQ(resource->get_value<std::string>("fragment_stage_id"), "deferred_gbuffer_shader");
    EXPECT_EQ(resource->get_value<std::string>("fragment_stage_entry"), "fragment_masked_main");
    EXPECT_TRUE(resource->get_value<bool>("depth_test"));
    EXPECT_TRUE(resource->get_value<bool>("depth_write"));
    EXPECT_TRUE(resource->get_value<bool>("cull_backfaces") == false);
}

} // namespace

TEST(WBEShaderMetadataVKTest, DeferredMaskedPipelineResourceUsesMaskedFragmentEntry) {
    expect_deferred_masked_pipeline_resource(Path("test_env/res/assets/shaders/deferred_gbuffer"));
}

TEST(WBEShaderMetadataVKTest, LoadsVertexInputRolesAndAutomaticallyAssignedLocations) {
    ParserJSON parser;
    parser.parse((Path("test_env/res/assets/shaders/vertex_input_attributes") / "vertex_input_attributes.json"));
    ShaderMetadata metadata;
    metadata.deserialize(parser.get_data());

    auto entry_point = std::ranges::find_if(metadata.entry_points, [](const ShaderMetadataEntryPoint& p_entry_point) {
        return p_entry_point.name == "vertex_main" && p_entry_point.stage == "vertex";
    });
    ASSERT_NE(entry_point, metadata.entry_points.end());
    ASSERT_FALSE(entry_point->parameters.empty());
    const auto& fields = entry_point->parameters[0].type.fields;
    const std::array<std::string_view, 5> expected_roles{"position", "normal", "tangent", "bitangent", "uv"};
    ASSERT_EQ(fields.size(), expected_roles.size());
    for (size_t i = 0; i < fields.size(); ++i) {
        EXPECT_EQ(fields[i].binding.kind, "varyingInput");
        EXPECT_EQ(fields[i].binding.index, static_cast<int32_t>(i));
        ASSERT_EQ(fields[i].user_attributes.size(), 1U);
        EXPECT_EQ(fields[i].user_attributes[0].name, "WBE_VERTEX_INPUT");
        ASSERT_EQ(fields[i].user_attributes[0].arguments.size(), 1U);
        EXPECT_EQ(fields[i].user_attributes[0].arguments[0].kind, "string");
        const std::string expected_argument = i == 0 ? std::string(expected_roles[i]) : std::string();
        EXPECT_EQ(fields[i].user_attributes[0].arguments[0].string_value, expected_argument);
    }
}

} // namespace WhiteBirdEngine

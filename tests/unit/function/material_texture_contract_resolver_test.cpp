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
#include "function/render/material_texture_contract.hh"
#include "function/render/material_texture_contract_resolver.hh"
#include "function/render/shader_metadata.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

static ShaderMetadataUserAttribute make_texture_contract_attribute(std::string_view p_role = {}) {
    ShaderMetadataUserAttribute result;
    result.name = "WBE_TEXTURE";
    if (!p_role.empty()) {
        ShaderMetadataUserAttributeArgument argument;
        argument.kind = "string";
        argument.string_value = p_role;
        result.arguments.push_back(std::move(argument));
    }
    return result;
}

static ShaderMetadataTypeField make_texture_contract_index_field(std::string_view p_name, std::string_view p_role, int32_t p_offset) {
    ShaderMetadataTypeField result;
    result.name = p_name;
    result.type.kind = "scalar";
    result.type.scalar_type = "int32";
    result.binding.kind = "uniform";
    result.binding.offset = p_offset;
    result.binding.size = sizeof(int32_t);
    result.user_attributes.push_back(make_texture_contract_attribute(p_role));
    return result;
}

static ShaderMetadataParameter make_material_params_test_parameter(int32_t p_stride = 16, std::string_view p_second_role = {}) {
    ShaderMetadataParameter result;
    result.name = "material_params";
    result.type.kind = "constantBuffer";
    ShaderMetadataTypeField materials;
    materials.name = "materials";
    materials.type.kind = "array";
    materials.type.element_count = 8;
    materials.binding.offset = 16;
    materials.binding.size = p_stride * materials.type.element_count;
    materials.binding.element_stride = p_stride;
    materials.fields.push_back(make_texture_contract_index_field("albedo_index", "albedo", 0));
    materials.fields.push_back(make_texture_contract_index_field("detail_index", p_second_role, 4));
    result.type.fields.push_back(std::move(materials));
    return result;
}

static ShaderMetadataParameter make_material_textures_test_parameter() {
    ShaderMetadataParameter result;
    result.name = "material_textures";
    result.type.kind = "parameterBlock";
    ShaderMetadataTypeField sampler;
    sampler.name = "material_sampler";
    sampler.type.kind = "samplerState";
    result.type.fields.push_back(std::move(sampler));
    ShaderMetadataTypeField textures;
    textures.name = "textures";
    textures.type.kind = "array";
    textures.type.element_scalar_type = "texture2D";
    result.type.fields.push_back(std::move(textures));
    return result;
}

static ShaderMetadata make_material_contract_metadata(int32_t p_stride = 16, bool p_include_textures = true) {
    ShaderMetadata result;
    result.parameters.push_back(make_material_params_test_parameter(p_stride));
    if (p_include_textures) {
        result.parameters.push_back(make_material_textures_test_parameter());
    }
    return result;
}

TEST(WBEMaterialTextureContractResolverTest, ResolvesExplicitAndDefaultRoles) {
    ShaderMetadata vertex_metadata = make_material_contract_metadata();
    ShaderMetadata fragment_metadata;
    const std::optional<MaterialTextureContract> contract = MaterialTextureContractResolver().resolve(
        vertex_metadata, nullptr, fragment_metadata, "material_textures", "material_params");

    ASSERT_TRUE(contract.has_value());
    EXPECT_EQ(contract->table_offset, 16);
    EXPECT_EQ(contract->table_stride, 16);
    EXPECT_EQ(contract->table_size, 128);
    EXPECT_EQ(contract->material_count, 8);
    ASSERT_EQ(contract->texture_fields.size(), 2);
    EXPECT_EQ(contract->texture_fields[0].role.get_name(), "albedo");
    EXPECT_EQ(contract->texture_fields[0].role.get_hash(), dynam_hash("albedo"));
    EXPECT_EQ(contract->texture_fields[0].offset, 0);
    EXPECT_EQ(contract->texture_fields[1].role.get_name(), "detail_index");
    EXPECT_EQ(contract->texture_fields[1].offset, 4);
}

TEST(WBEMaterialTextureContractResolverTest, AllowsAContractToBeAbsent) {
    ShaderMetadata metadata;
    const auto contract = MaterialTextureContractResolver().resolve(metadata, nullptr, metadata, "material_textures", "material_params");
    EXPECT_FALSE(contract.has_value());
}

TEST(WBEMaterialTextureContractResolverTest, RejectsMissingAndIncompatibleGlobalContracts) {
    ShaderMetadata params_only = make_material_contract_metadata(16, false);
    ShaderMetadata empty;
    EXPECT_THROW(MaterialTextureContractResolver().resolve(params_only, nullptr, empty, "material_textures", "material_params"),
        std::runtime_error);

    ShaderMetadata vertex_metadata = make_material_contract_metadata(16);
    ShaderMetadata fragment_metadata = make_material_contract_metadata(32);
    EXPECT_THROW(MaterialTextureContractResolver().resolve(vertex_metadata, nullptr, fragment_metadata, "material_textures", "material_params"),
        std::runtime_error);
}

TEST(WBEMaterialTextureContractResolverTest, RejectsInvalidTextureFieldsAndDuplicateRoles) {
    ShaderMetadata invalid_type = make_material_contract_metadata();
    invalid_type.parameters[0].type.fields[0].fields[0].type.scalar_type = "float32";
    ShaderMetadata empty;
    EXPECT_THROW(MaterialTextureContractResolver().resolve(invalid_type, nullptr, empty, "material_textures", "material_params"),
        std::runtime_error);

    ShaderMetadata duplicate_role = make_material_contract_metadata();
    duplicate_role.parameters[0].type.fields[0].fields[1].user_attributes[0].arguments.clear();
    ShaderMetadataUserAttributeArgument duplicate_role_argument;
    duplicate_role_argument.kind = "string";
    duplicate_role_argument.string_value = "albedo";
    duplicate_role.parameters[0].type.fields[0].fields[1].user_attributes[0].arguments.push_back(std::move(duplicate_role_argument));
    EXPECT_THROW(MaterialTextureContractResolver().resolve(duplicate_role, nullptr, empty, "material_textures", "material_params"),
        std::runtime_error);
}

} // namespace WhiteBirdEngine

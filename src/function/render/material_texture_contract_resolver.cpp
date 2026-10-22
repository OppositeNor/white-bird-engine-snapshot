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
#include "function/render/material_texture_contract_resolver.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_query.hh"
#include "function/render/shader_role_field_resolver.hh"
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

struct MaterialTextureReflection {
    std::optional<MaterialTextureContract> contract;
    bool has_material_params = false;
    bool has_material_textures = false;
};

static MaterialTextureContract reflect_material_texture_contract(const ShaderMetadataParameter& p_material_params) {
    ShaderMetadataQuery metadata_query;
    if (p_material_params.type.kind != "constantBuffer" || p_material_params.type.fields.size() != 1) {
        throw std::runtime_error(
            "Failed to resolve material texture contract: material params must be a constant buffer containing only materials.");
    }
    const ShaderMetadataTypeField* materials_field = metadata_query.find_field(p_material_params.type.fields, "materials");
    if (materials_field == nullptr || materials_field->type.kind != "array" || materials_field->type.element_count <= 0 ||
        materials_field->binding.offset < 0 || materials_field->binding.size <= 0 || materials_field->binding.element_stride <= 0) {
        throw std::runtime_error(
            "Failed to resolve material texture contract: materials must be a sized array with a reflected uniform layout.");
    }
    MaterialTextureContract result;
    result.table_offset = static_cast<size_t>(materials_field->binding.offset);
    result.table_stride = static_cast<size_t>(materials_field->binding.element_stride);
    result.table_size = static_cast<size_t>(materials_field->binding.size);
    result.material_count = static_cast<size_t>(materials_field->type.element_count);
    if (result.material_count > std::numeric_limits<size_t>::max() / result.table_stride ||
        result.table_size < result.table_stride * result.material_count) {
        throw std::runtime_error("Failed to resolve material texture contract: reflected material table is too small.");
    }
    ShaderRoleFieldResolver role_resolver;
    std::vector<ShaderRoleFieldResolver::Field> fields =
        role_resolver.collect_fields(materials_field->fields, "WBE_TEXTURE", ShaderRoleFieldResolver::Traversal::DIRECT);
    for (ShaderRoleFieldResolver::Field& resolved_field : fields) {
        const ShaderMetadataTypeField& field = *resolved_field.metadata;
        if (field.type.kind != "scalar" || field.type.scalar_type != "int32" || field.binding.kind != "uniform" ||
            field.binding.offset < 0 || std::cmp_not_equal(field.binding.size, sizeof(int32_t))) {
            throw std::runtime_error(std::format("Failed to resolve material texture contract: WBE_TEXTURE field '{}' must be a 32-bit uniform integer.",
                field.name));
        }
        const size_t field_offset = static_cast<size_t>(field.binding.offset);
        if (field_offset > result.table_stride || sizeof(int32_t) > result.table_stride - field_offset) {
            throw std::runtime_error(std::format(
                "Failed to resolve material texture contract: WBE_TEXTURE field '{}' exceeds the material stride.", field.name));
        }
        result.texture_fields.push_back(
            MaterialTextureContract::TextureField{.role = std::move(resolved_field.role), .offset = field_offset});
    }
    if (result.texture_fields.empty()) {
        throw std::runtime_error("Failed to resolve material texture contract: MaterialParams has no WBE_TEXTURE fields.");
    }
    return result;
}

static void validate_material_textures(const ShaderMetadataParameter& p_material_textures) {
    ShaderMetadataQuery metadata_query;
    const ShaderMetadataTypeField* sampler_field = metadata_query.find_field(p_material_textures.type.fields, "material_sampler");
    const ShaderMetadataTypeField* textures_field = metadata_query.find_field(p_material_textures.type.fields, "textures");
    if (p_material_textures.type.kind != "parameterBlock" || sampler_field == nullptr || textures_field == nullptr ||
        sampler_field->type.kind != "samplerState" || textures_field->type.kind != "array" ||
        !textures_field->type.element_scalar_type.starts_with("texture")) {
        throw std::runtime_error(
            "Failed to resolve material texture contract: material textures must contain material_sampler and textures[].");
    }
}

static void merge_stage_reflection(MaterialTextureReflection& p_result,
    const ShaderMetadata& p_metadata,
    std::string_view p_material_textures_variable_name,
    std::string_view p_material_params_variable_name) {
    ShaderMetadataQuery metadata_query;
    const ShaderMetadataParameter* material_params = metadata_query.find_parameter(p_metadata, p_material_params_variable_name);
    const ShaderMetadataParameter* material_textures = metadata_query.find_parameter(p_metadata, p_material_textures_variable_name);
    if (material_params != nullptr) {
        MaterialTextureContract contract = reflect_material_texture_contract(*material_params);
        if (p_result.contract.has_value() && !p_result.contract->matches(contract)) {
            throw std::runtime_error("Failed to resolve material texture contract: shader stages use incompatible material layouts.");
        }
        p_result.contract = std::move(contract);
        p_result.has_material_params = true;
    }
    if (material_textures != nullptr) {
        validate_material_textures(*material_textures);
        p_result.has_material_textures = true;
    }
}

std::optional<MaterialTextureContract> MaterialTextureContractResolver::resolve(const ShaderMetadata& p_vertex_metadata,
    const ShaderMetadata* p_geometry_metadata,
    const ShaderMetadata& p_fragment_metadata,
    std::string_view p_material_textures_variable_name,
    std::string_view p_material_params_variable_name) const {
    if (p_material_textures_variable_name.empty() || p_material_params_variable_name.empty()) {
        throw std::runtime_error("Failed to resolve material texture contract: global variable names must be non-empty.");
    }
    MaterialTextureReflection reflection;
    merge_stage_reflection(reflection, p_vertex_metadata, p_material_textures_variable_name, p_material_params_variable_name);
    if (p_geometry_metadata != nullptr) {
        merge_stage_reflection(reflection, *p_geometry_metadata, p_material_textures_variable_name, p_material_params_variable_name);
    }
    merge_stage_reflection(reflection, p_fragment_metadata, p_material_textures_variable_name, p_material_params_variable_name);
    if (reflection.has_material_params != reflection.has_material_textures) {
        throw std::runtime_error(
            "Failed to resolve material texture contract: material_params and material_textures must be declared together.");
    }
    return std::move(reflection.contract);
}

} // namespace WhiteBirdEngine

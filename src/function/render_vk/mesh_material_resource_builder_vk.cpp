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
#include "function/render_vk/mesh_material_resource_builder_vk.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_role.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {
namespace {

std::string material_label(RefRes<RUMaterial> p_material, size_t p_material_index) {
    if (p_material != nullptr && p_material->resource_key != nullptr && p_material->resource_key[0] != '\0') {
        return std::string(p_material->resource_key);
    }
    return std::format("material[{}]", p_material_index);
}

std::span<const RUMaterial::TextureSlot> get_texture_slots(RefRes<RUMaterial> p_material, std::string_view p_material_name) {
    if (p_material == nullptr) {
        throw std::runtime_error(std::format("Failed to build mesh material bindings: '{}' is missing.", p_material_name));
    }
    if (p_material->texture_count > 0 && p_material->textures == nullptr) {
        throw std::runtime_error(
            std::format("Failed to build mesh material bindings: '{}' texture slots are missing.", p_material_name));
    }
    return std::span<const RUMaterial::TextureSlot>(p_material->textures.get(), p_material->texture_count);
}

const RUMaterial::TextureSlot* find_texture_slot(
    std::span<const RUMaterial::TextureSlot> p_texture_slots, const ShaderRole& p_role, std::string_view p_material_name) {
    const RUMaterial::TextureSlot* matched_slot = nullptr;
    for (const RUMaterial::TextureSlot& texture_slot : p_texture_slots) {
        const std::string_view role_name = texture_slot.role.get_name();
        if (!p_role.matches(role_name, texture_slot.role.hash)) {
            continue;
        }
        if (texture_slot.texture == nullptr) {
            throw std::runtime_error(std::format(
                "Failed to build mesh material bindings: '{}' role '{}' has no texture.", p_material_name, p_role.get_name()));
        }
        if (matched_slot != nullptr) {
            throw std::runtime_error(std::format(
                "Failed to build mesh material bindings: '{}' repeats role '{}'.", p_material_name, p_role.get_name()));
        }
        matched_slot = &texture_slot;
    }
    return matched_slot;
}

uint32_t get_or_append_texture_descriptor_index(std::vector<RefRes<RUTexture>>& p_textures,
    RefRes<RUTexture> p_texture,
    uint32_t p_max_texture_descriptor_count,
    std::string_view p_role_name,
    std::string_view p_material_name) {
    for (size_t i = 0; i < p_textures.size(); ++i) {
        if (p_textures[i] == p_texture) {
            return static_cast<uint32_t>(i);
        }
    }
    if (p_textures.size() >= p_max_texture_descriptor_count) {
        throw std::runtime_error(std::format("Failed to build mesh material bindings: runtime texture table exceeds descriptor limit {} while adding role '{}' from '{}'.",
            p_max_texture_descriptor_count,
            p_role_name,
            p_material_name));
    }
    p_textures.push_back(p_texture);
    return static_cast<uint32_t>(p_textures.size() - 1);
}

void write_texture_index(std::vector<uint8_t>& p_buffer, size_t p_offset, uint32_t p_value) {
    int32_t signed_value = static_cast<int32_t>(p_value);
    std::memcpy(p_buffer.data() + p_offset, &signed_value, sizeof(signed_value));
}

} // namespace

MeshMaterialResourcesVK MeshMaterialResourceBuilderVK::build(const std::optional<MaterialTextureContract>& p_material_contract,
    std::span<const RefRes<RUMaterial>> p_materials,
    RefRes<RUMaterial> p_default_material,
    uint32_t p_max_texture_descriptor_count,
    Ref<IRenderObject> p_material_sampler,
    const TextureDescriptorIndexResolver& p_texture_descriptor_index_resolver) const {
    if (!p_material_contract.has_value()) {
        return {};
    }

    const MaterialTextureContract& material_contract = *p_material_contract;
    if (p_materials.size() > material_contract.material_count) {
        throw std::runtime_error(std::format("Failed to build mesh material bindings: material row count {} exceeds shader capacity {}.",
            p_materials.size(),
            material_contract.material_count));
    }

    std::span<const RUMaterial::TextureSlot> default_texture_slots = get_texture_slots(p_default_material, "default material");
    std::vector<RefRes<RUTexture>> unique_textures;
    unique_textures.reserve(default_texture_slots.size() + p_materials.size() * material_contract.texture_fields.size());

    std::vector<uint32_t> default_texture_indices;
    default_texture_indices.reserve(material_contract.texture_fields.size());
    for (const MaterialTextureContract::TextureField& texture_field : material_contract.texture_fields) {
        const RUMaterial::TextureSlot* default_slot = find_texture_slot(default_texture_slots, texture_field.role, "default material");
        if (default_slot == nullptr) {
            throw std::runtime_error(std::format("Failed to build mesh material bindings: default material is missing required role '{}'.",
                texture_field.role.get_name()));
        }
        default_texture_indices.push_back(get_or_append_texture_descriptor_index(
            unique_textures, default_slot->texture, p_max_texture_descriptor_count, texture_field.role.get_name(), "default material"));
    }

    std::vector<uint8_t> material_params_data(material_contract.table_size, uint8_t{0});
    for (size_t material_index = 0; material_index < material_contract.material_count; ++material_index) {
        const size_t row_offset = material_contract.table_offset + material_index * material_contract.table_stride;
        for (size_t field_index = 0; field_index < material_contract.texture_fields.size(); ++field_index) {
            write_texture_index(material_params_data,
                row_offset + material_contract.texture_fields[field_index].offset,
                default_texture_indices[field_index]);
        }
    }

    for (size_t material_index = 0; material_index < p_materials.size(); ++material_index) {
        const RefRes<RUMaterial>& material = p_materials[material_index];
        const std::string bound_material_name = material_label(material, material_index);
        std::span<const RUMaterial::TextureSlot> texture_slots = get_texture_slots(material, bound_material_name);
        const size_t row_offset = material_contract.table_offset + material_index * material_contract.table_stride;
        for (const MaterialTextureContract::TextureField& texture_field : material_contract.texture_fields) {
            const RUMaterial::TextureSlot* matched_slot = find_texture_slot(texture_slots, texture_field.role, bound_material_name);
            if (matched_slot == nullptr) {
                continue;
            }
            const uint32_t descriptor_index = get_or_append_texture_descriptor_index(
                unique_textures, matched_slot->texture, p_max_texture_descriptor_count, texture_field.role.get_name(), bound_material_name);
            write_texture_index(material_params_data, row_offset + texture_field.offset, descriptor_index);
        }
    }

    if (p_texture_descriptor_index_resolver) {
        for (size_t material_index = 0; material_index < material_contract.material_count; ++material_index) {
            const size_t row_offset = material_contract.table_offset + material_index * material_contract.table_stride;
            for (const MaterialTextureContract::TextureField& texture_field : material_contract.texture_fields) {
                int32_t texture_index = 0;
                std::memcpy(&texture_index, material_params_data.data() + row_offset + texture_field.offset, sizeof(texture_index));
                if (texture_index < 0 || static_cast<size_t>(texture_index) >= unique_textures.size()) {
                    throw std::runtime_error("Failed to build mesh material bindings: generated texture index is invalid.");
                }
                write_texture_index(material_params_data,
                    row_offset + texture_field.offset,
                    p_texture_descriptor_index_resolver(unique_textures[static_cast<size_t>(texture_index)]));
            }
        }
    }

    return MeshMaterialResourcesVK{.material_params_data = std::move(material_params_data),
        .material_textures = std::move(unique_textures),
        .material_sampler = std::move(p_material_sampler)};
}

} // namespace WhiteBirdEngine

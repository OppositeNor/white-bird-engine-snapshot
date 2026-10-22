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
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_query.hh"
#include "function/render/vertex_input_contract.hh"
#include "function/render/vertex_input_contract_resolver.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include "generated/enum_reflection.gen.hh"
#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

bool entry_point_uses_push_data(const ShaderMetadataEntryPoint& p_entry_point, const ShaderMetadataBinding& p_binding) {
    if (p_entry_point.bindings.empty()) {
        return true;
    }
    return std::ranges::any_of(p_entry_point.bindings, [&p_binding](const ShaderMetadataBinding& p_entry_point_binding) {
        return p_entry_point_binding.kind == "pushConstantBuffer" && p_entry_point_binding.index == p_binding.index;
    });
}

ShaderDescriptorHeapTypeVK descriptor_heap_type_from_name(std::string_view p_name) {
    const std::optional<ShaderDescriptorHeapTypeVK> type = EnumReflection<ShaderDescriptorHeapTypeVK>::try_string_to_enum(p_name);
    if (!type.has_value()) {
        throw std::runtime_error(std::format("Failed to resolve WBE_DESCRIPTOR: unsupported descriptor type '{}'.", p_name));
    }
    return *type;
}

bool descriptor_type_is_writable(ShaderDescriptorHeapTypeVK p_type) {
    return p_type == ShaderDescriptorHeapTypeVK::STORAGE_BUFFER || p_type == ShaderDescriptorHeapTypeVK::STORAGE_IMAGE;
}

ShaderDescriptorAccessVK descriptor_access(
    ShaderDescriptorHeapTypeVK p_type, const ShaderMetadataUserAttribute& p_attribute, std::string_view p_field_path) {
    // Slang reflects an omitted access argument as its empty default value.
    if (p_attribute.arguments.size() < 3 || p_attribute.arguments[2].string_value.empty()) {
        return descriptor_type_is_writable(p_type) ? ShaderDescriptorAccessVK::READ_WRITE : ShaderDescriptorAccessVK::READ;
    }
    const std::string_view name = p_attribute.arguments[2].string_value;
    const std::optional<ShaderDescriptorAccessVK> parsed_access = EnumReflection<ShaderDescriptorAccessVK>::try_string_to_enum(name);
    if (!parsed_access.has_value()) {
        throw std::runtime_error(
            std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' has unsupported access '{}'.", p_field_path, name));
    }
    const ShaderDescriptorAccessVK access = *parsed_access;
    if (access != ShaderDescriptorAccessVK::READ && !descriptor_type_is_writable(p_type)) {
        throw std::runtime_error(
            std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' writes a descriptor type that is read-only.", p_field_path));
    }
    return access;
}

ShaderDescriptorAccessVK combine_accesses(ShaderDescriptorAccessVK p_first, ShaderDescriptorAccessVK p_second) {
    return p_first == p_second ? p_first : ShaderDescriptorAccessVK::READ_WRITE;
}

const ShaderMetadataUserAttribute* find_unique_attribute(const ShaderMetadataTypeField& p_field, std::string_view p_attribute_name) {
    const ShaderMetadataUserAttribute* result = nullptr;
    for (const ShaderMetadataUserAttribute& attribute : p_field.user_attributes) {
        if (attribute.name != p_attribute_name) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(
                std::format("Failed to resolve {}: field '{}' repeats the annotation.", p_attribute_name, p_field.name));
        }
        result = &attribute;
    }
    return result;
}

bool ranges_overlap(uint32_t p_first_offset, uint32_t p_first_size, uint32_t p_second_offset, uint32_t p_second_size) {
    const uint64_t first_offset = p_first_offset;
    const uint64_t second_offset = p_second_offset;
    return first_offset < second_offset + p_second_size && second_offset < first_offset + p_first_size;
}

bool fields_overlap(const ShaderDescriptorHeapFieldVK& p_first, const ShaderDescriptorHeapFieldVK& p_second) {
    return ranges_overlap(p_first.offset, sizeof(uint32_t) * 2, p_second.offset, sizeof(uint32_t) * 2);
}

bool fields_overlap(const ShaderDefaultPushFieldVK& p_first, const ShaderDefaultPushFieldVK& p_second) {
    return ranges_overlap(p_first.offset, sizeof(uint32_t), p_second.offset, sizeof(uint32_t));
}

bool fields_overlap(const ShaderDescriptorHeapFieldVK& p_first, const ShaderDefaultPushFieldVK& p_second) {
    return ranges_overlap(p_first.offset, sizeof(uint32_t) * 2, p_second.offset, sizeof(uint32_t));
}

void append_descriptor_field(ShaderDescriptorHeapLayoutVK& p_layout, ShaderDescriptorHeapFieldVK p_field) {
    for (const ShaderDescriptorHeapFieldVK& existing : p_layout.fields) {
        if (existing.field_path == p_field.field_path) {
            throw std::runtime_error(
                std::format("Failed to resolve WBE_DESCRIPTOR: field path '{}' is declared more than once.", p_field.field_path));
        }
        if (fields_overlap(existing, p_field)) {
            throw std::runtime_error(std::format(
                "Failed to resolve WBE_DESCRIPTOR: fields '{}' and '{}' overlap in push data.", existing.field_path, p_field.field_path));
        }
    }
    for (const ShaderDefaultPushFieldVK& existing : p_layout.default_fields) {
        if (fields_overlap(p_field, existing)) {
            throw std::runtime_error(std::format(
                "Failed to resolve push data: fields '{}' and '{}' overlap.", p_field.field_path, existing.field_path));
        }
    }
    p_layout.fields.push_back(std::move(p_field));
}

ShaderDefaultPushValueVK default_push_value_from_name(std::string_view p_name) {
    if (p_name == "time") {
        return ShaderDefaultPushValueVK::TIME;
    }
    if (p_name == "frame_slot") {
        return ShaderDefaultPushValueVK::FRAME_SLOT;
    }
    return ShaderDefaultPushValueVK::CUSTOM;
}

std::optional<ShaderPushConstantTypeVK> push_constant_type_from_scalar_type(std::string_view p_scalar_type) {
    if (p_scalar_type == "uint32") {
        return ShaderPushConstantTypeVK::UINT32;
    }
    if (p_scalar_type == "int32") {
        return ShaderPushConstantTypeVK::INT32;
    }
    if (p_scalar_type == "float32") {
        return ShaderPushConstantTypeVK::FLOAT32;
    }
    return std::nullopt;
}

bool push_constant_type_is_valid(ShaderDefaultPushValueVK p_value, ShaderPushConstantTypeVK p_type) {
    switch (p_value) {
    case ShaderDefaultPushValueVK::TIME:
        return p_type == ShaderPushConstantTypeVK::FLOAT32;
    case ShaderDefaultPushValueVK::FRAME_SLOT:
        return p_type == ShaderPushConstantTypeVK::UINT32;
    case ShaderDefaultPushValueVK::CUSTOM:
        return true;
    }
    return false;
}

void append_default_push_field(ShaderDescriptorHeapLayoutVK& p_layout, ShaderDefaultPushFieldVK p_field) {
    for (const ShaderDefaultPushFieldVK& existing : p_layout.default_fields) {
        if (existing.field_path == p_field.field_path) {
            throw std::runtime_error(std::format(
                "Failed to resolve WBE_PUSH_CONSTANT: field path '{}' is declared more than once.", p_field.field_path));
        }
        if (fields_overlap(existing, p_field)) {
            throw std::runtime_error(std::format("Failed to resolve WBE_PUSH_CONSTANT: fields '{}' and '{}' overlap in push data.",
                existing.field_path,
                p_field.field_path));
        }
    }
    for (const ShaderDescriptorHeapFieldVK& existing : p_layout.fields) {
        if (fields_overlap(existing, p_field)) {
            throw std::runtime_error(std::format(
                "Failed to resolve push data: fields '{}' and '{}' overlap.", existing.field_path, p_field.field_path));
        }
    }
    p_layout.default_fields.push_back(std::move(p_field));
}

// A resource id ending in "[]" is resolved per element of the nearest enclosing descriptor struct array.
constexpr std::string_view INDEXED_RESOURCE_ID_SUFFIX = "[]";

std::string resolve_descriptor_resource_id(
    std::string_view p_resource_id, std::optional<uint32_t> p_array_index, std::string_view p_field_path) {
    if (!p_resource_id.ends_with(INDEXED_RESOURCE_ID_SUFFIX)) {
        return std::string(p_resource_id);
    }
    if (p_resource_id.size() == INDEXED_RESOURCE_ID_SUFFIX.size()) {
        throw std::runtime_error(
            std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' has an indexed resource id without a name.", p_field_path));
    }
    if (!p_array_index.has_value()) {
        throw std::runtime_error(std::format(
            "Failed to resolve WBE_DESCRIPTOR: field '{}' has an indexed resource id outside a descriptor struct array.", p_field_path));
    }
    p_resource_id.remove_suffix(INDEXED_RESOURCE_ID_SUFFIX.size());
    return std::format("{}[{}]", p_resource_id, *p_array_index);
}

uint32_t collect_descriptor_heap_fields(const std::vector<ShaderMetadataTypeField>& p_fields,
    std::string_view p_parent_path,
    uint32_t p_parent_offset,
    std::optional<uint32_t> p_array_index,
    ShaderDescriptorHeapLayoutVK& p_layout) {
    uint64_t extent = p_parent_offset;
    for (const ShaderMetadataTypeField& field : p_fields) {
        if (field.binding.offset < 0 || field.binding.size < 0) {
            throw std::runtime_error(
                std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' has a negative layout value.", field.name));
        }
        uint64_t field_offset = static_cast<uint64_t>(p_parent_offset) + static_cast<uint32_t>(field.binding.offset);
        uint64_t field_extent = field_offset + static_cast<uint32_t>(field.binding.size);
        if (field_offset > std::numeric_limits<uint32_t>::max() || field_extent > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(
                std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' exceeds the supported push-data size.", field.name));
        }
        extent = std::max(extent, field_extent);

        std::string field_path = p_parent_path.empty() ? field.name : std::format("{}.{}", p_parent_path, field.name);
        const ShaderMetadataUserAttribute* descriptor_attribute = find_unique_attribute(field, "WBE_DESCRIPTOR");
        const ShaderMetadataUserAttribute* default_push_attribute = find_unique_attribute(field, "WBE_PUSH_CONSTANT");
        if (descriptor_attribute != nullptr && default_push_attribute != nullptr) {
            throw std::runtime_error(
                std::format("Failed to resolve push data: field '{}' has both WBE_DESCRIPTOR and WBE_PUSH_CONSTANT.", field_path));
        }
        if (descriptor_attribute != nullptr) {
            const std::vector<ShaderMetadataUserAttributeArgument>& arguments = descriptor_attribute->arguments;
            if (arguments.size() < 2 || arguments.size() > 3 ||
                std::ranges::any_of(arguments,
                    [](const ShaderMetadataUserAttributeArgument& p_argument) {
                        return p_argument.kind != "string";
                    }) ||
                arguments[0].string_value.empty() || arguments[1].string_value.empty()) {
                throw std::runtime_error(std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' requires descriptor-type and resource-id non-empty string arguments and an optional access argument.",
                    field_path));
            }
            if (field.type.kind != "vector" || field.type.element_scalar_type != "uint32" || field.type.element_count != 2 ||
                field.binding.kind != "uniform" || std::cmp_not_equal(field.binding.size, sizeof(uint32_t) * 2) ||
                field_offset % (sizeof(uint32_t) * 2) != 0) {
                throw std::runtime_error(
                    std::format("Failed to resolve WBE_DESCRIPTOR: field '{}' must be an aligned eight-byte uint2.", field_path));
            }
            const ShaderDescriptorHeapTypeVK type = descriptor_heap_type_from_name(arguments[0].string_value);
            append_descriptor_field(p_layout,
                ShaderDescriptorHeapFieldVK{
                    .field_path = field_path,
                    .type = type,
                    .resource_id = resolve_descriptor_resource_id(arguments[1].string_value, p_array_index, field_path),
                    .offset = static_cast<uint32_t>(field_offset),
                    .access = descriptor_access(type, *descriptor_attribute, field_path),
                });
        }
        if (default_push_attribute != nullptr) {
            if (default_push_attribute->arguments.size() != 1 || default_push_attribute->arguments[0].kind != "string" ||
                default_push_attribute->arguments[0].string_value.empty()) {
                throw std::runtime_error(std::format(
                    "Failed to resolve WBE_PUSH_CONSTANT: field '{}' requires one non-empty string argument.", field_path));
            }
            const std::string& value_name = default_push_attribute->arguments[0].string_value;
            const ShaderDefaultPushValueVK value = default_push_value_from_name(value_name);
            const std::optional<ShaderPushConstantTypeVK> type = push_constant_type_from_scalar_type(field.type.scalar_type);
            if (field.type.kind != "scalar" || !type.has_value() || !push_constant_type_is_valid(value, *type) ||
                field.binding.kind != "uniform" || std::cmp_not_equal(field.binding.size, sizeof(uint32_t)) ||
                field_offset % sizeof(uint32_t) != 0) {
                throw std::runtime_error(std::format(
                    "Failed to resolve WBE_PUSH_CONSTANT: field '{}' has an invalid type or four-byte scalar layout.", field_path));
            }
            append_default_push_field(p_layout,
                ShaderDefaultPushFieldVK{
                    .field_path = field_path,
                    .value = value,
                    .name = value == ShaderDefaultPushValueVK::CUSTOM ? value_name : std::string(),
                    .type = *type,
                    .offset = static_cast<uint32_t>(field_offset),
                });
        }
        if (field.type.kind == "array" && field.type.element_count > 0 && !field.fields.empty()) {
            if (field.binding.element_stride <= 0) {
                throw std::runtime_error(std::format(
                    "Failed to resolve WBE_DESCRIPTOR: descriptor struct array '{}' has no element stride.", field_path));
            }
            for (int32_t i = 0; i < field.type.element_count; ++i) {
                uint64_t element_offset = field_offset + static_cast<uint64_t>(i) * field.binding.element_stride;
                if (element_offset > std::numeric_limits<uint32_t>::max()) {
                    throw std::runtime_error(std::format(
                        "Failed to resolve WBE_DESCRIPTOR: descriptor struct array '{}' exceeds push data.", field_path));
                }
                extent = std::max<uint64_t>(extent,
                    collect_descriptor_heap_fields(
                        field.fields, std::format("{}[{}]", field_path, i), static_cast<uint32_t>(element_offset), static_cast<uint32_t>(i), p_layout));
            }
        } else {
            extent = std::max<uint64_t>(extent,
                collect_descriptor_heap_fields(field.fields, field_path, static_cast<uint32_t>(field_offset), p_array_index, p_layout));
        }
    }
    return static_cast<uint32_t>(extent);
}

void merge_descriptor_heap_layout(ShaderDescriptorHeapLayoutVK& p_result, const ShaderDescriptorHeapLayoutVK& p_stage_layout) {
    p_result.size = std::max(p_result.size, p_stage_layout.size);
    p_result.stage_flags |= p_stage_layout.stage_flags;
    for (const ShaderDescriptorHeapFieldVK& field : p_stage_layout.fields) {
        auto existing = std::ranges::find_if(p_result.fields, [&field](const ShaderDescriptorHeapFieldVK& p_existing) {
            return p_existing.field_path == field.field_path;
        });
        if (existing != p_result.fields.end()) {
            if (existing->type != field.type || existing->resource_id != field.resource_id || existing->offset != field.offset) {
                throw std::runtime_error(
                    std::format("Conflicting descriptor-heap push-data declarations for field '{}'.", field.field_path));
            }
            existing->access = combine_accesses(existing->access, field.access);
            continue;
        }
        for (const ShaderDescriptorHeapFieldVK& other : p_result.fields) {
            if (fields_overlap(other, field)) {
                throw std::runtime_error(std::format(
                    "Conflicting descriptor-heap push-data fields '{}' and '{}' overlap.", other.field_path, field.field_path));
            }
        }
        for (const ShaderDefaultPushFieldVK& other : p_result.default_fields) {
            if (fields_overlap(field, other)) {
                throw std::runtime_error(
                    std::format("Conflicting push-data fields '{}' and '{}' overlap.", field.field_path, other.field_path));
            }
        }
        p_result.fields.push_back(field);
    }
    for (const ShaderDefaultPushFieldVK& field : p_stage_layout.default_fields) {
        auto existing = std::ranges::find_if(p_result.default_fields, [&field](const ShaderDefaultPushFieldVK& p_existing) {
            return p_existing.field_path == field.field_path;
        });
        if (existing != p_result.default_fields.end()) {
            if (existing->value != field.value || existing->name != field.name || existing->type != field.type ||
                existing->offset != field.offset) {
                throw std::runtime_error(std::format("Conflicting default push-data declarations for field '{}'.", field.field_path));
            }
            continue;
        }
        for (const ShaderDefaultPushFieldVK& other : p_result.default_fields) {
            if (fields_overlap(other, field)) {
                throw std::runtime_error(std::format(
                    "Conflicting default push-data fields '{}' and '{}' overlap.", other.field_path, field.field_path));
            }
        }
        for (const ShaderDescriptorHeapFieldVK& other : p_result.fields) {
            if (fields_overlap(other, field)) {
                throw std::runtime_error(
                    std::format("Conflicting push-data fields '{}' and '{}' overlap.", other.field_path, field.field_path));
            }
        }
        p_result.default_fields.push_back(field);
    }
}

void append_stage_descriptor_heap_layout(
    ShaderDescriptorHeapLayoutVK& p_layout, const ShaderMetadata* p_metadata, std::string_view p_entry_name, std::string_view p_stage) {
    if (p_metadata == nullptr) {
        return;
    }
    std::string entry_name = p_entry_name.data() == nullptr ? std::string() : std::string(p_entry_name);
    merge_descriptor_heap_layout(p_layout, collect_shader_descriptor_heap_layout(*p_metadata, p_stage, entry_name));
}

} // namespace

VkShaderStageFlagBits shader_stage_flag_from_metadata_stage(std::string_view p_stage) {
    if (p_stage == "vertex" || p_stage == "vert") {
        return VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (p_stage == "fragment" || p_stage == "frag") {
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (p_stage == "geometry" || p_stage == "geom") {
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    if (p_stage == "tesscontrol" || p_stage == "tesc") {
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    if (p_stage == "tesseval" || p_stage == "tese") {
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    if (p_stage == "compute" || p_stage == "comp") {
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    throw std::runtime_error(std::format("Unsupported shader stage '{}'.", p_stage));
}

VertexInputDescriptionVK build_vertex_input_description_vk(
    const ShaderMetadata& p_metadata, std::string_view p_entry_name, std::span<const RUVertexView> p_vertex_bindings) {
    const VertexInputContract contract = VertexInputContractResolver().resolve(p_metadata, p_entry_name, p_vertex_bindings);
    return VertexInputDescriptionVK::from_contract(contract);
}

VertexInputDescriptionVK build_vertex_input_description_vk(
    const VertexInputRequirement& p_requirement, std::span<const RUVertexView> p_vertex_bindings) {
    return VertexInputDescriptionVK::from_contract(VertexInputContractResolver().resolve(p_requirement, p_vertex_bindings));
}

ShaderDescriptorHeapLayoutVK collect_shader_descriptor_heap_layout(
    const ShaderMetadata& p_metadata, std::string_view p_stage, std::string_view p_entry_name) {
    const ShaderMetadataEntryPoint& entry_point = ShaderMetadataQuery().resolve_entry_point(p_metadata, p_stage, p_entry_name);
    const ShaderMetadataParameter* push_data = nullptr;
    for (const ShaderMetadataParameter& parameter : p_metadata.parameters) {
        if (parameter.binding.kind != "pushConstantBuffer" || !entry_point_uses_push_data(entry_point, parameter.binding)) {
            continue;
        }
        if (push_data != nullptr) {
            throw std::runtime_error(
                std::format("Shader entry point '{}': more than one push-data parameter is reflected.", entry_point.name));
        }
        push_data = &parameter;
    }
    ShaderDescriptorHeapLayoutVK result;
    if (push_data == nullptr) {
        return result;
    }
    if (push_data->type.kind != "constantBuffer") {
        throw std::runtime_error(
            std::format("Shader push-data parameter '{}': expected constantBuffer type metadata.", push_data->name));
    }
    result.stage_flags = shader_stage_flag_from_metadata_stage(entry_point.stage);
    result.size = collect_descriptor_heap_fields(push_data->type.fields, {}, 0, std::nullopt, result);
    return result;
}

ShaderDescriptorHeapLayoutVK collect_graphics_pipeline_descriptor_heap_layout(const ShaderMetadata& p_vertex_shader_metadata,
    std::string_view p_vertex_stage_entry,
    const ShaderMetadata* p_geometry_shader_metadata,
    std::string_view p_geometry_stage_entry,
    const ShaderMetadata& p_fragment_shader_metadata,
    std::string_view p_fragment_stage_entry) {
    ShaderDescriptorHeapLayoutVK result;
    append_stage_descriptor_heap_layout(result, &p_vertex_shader_metadata, p_vertex_stage_entry, "vertex");
    append_stage_descriptor_heap_layout(result, p_geometry_shader_metadata, p_geometry_stage_entry, "geometry");
    append_stage_descriptor_heap_layout(result, &p_fragment_shader_metadata, p_fragment_stage_entry, "fragment");
    return result;
}

} // namespace WhiteBirdEngine

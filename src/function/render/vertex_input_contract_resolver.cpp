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
#include "function/render/vertex_input_contract_resolver.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_query.hh"
#include "function/render/shader_role_field_resolver.hh"
#include "function/render/vertex_attribute_format_info.hh"
#include "function/render/vertex_input_contract.hh"
#include "function/render/vertex_input_requirement.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

struct VertexAttributeLayoutView {
    std::string_view role_name;
    HashCode role_hash = 0;
    RUVertexAttributeFormat format = RUVertexAttributeFormat::FLOAT;
    size_t offset = 0;
    size_t size = 0;
};

struct VertexLayoutView {
    std::vector<VertexAttributeLayoutView> attributes;
    size_t vertex_stride = 0;
};

static VertexLayoutView make_vertex_layout_view(const RUVertexView& p_vertex_data) {
    if (p_vertex_data.attribute_count > 0 && p_vertex_data.attributes == nullptr) {
        throw std::runtime_error("Failed to resolve vertex input contract: resource attribute metadata is missing.");
    }
    VertexLayoutView result;
    result.vertex_stride = p_vertex_data.vertex_stride;
    result.attributes.reserve(p_vertex_data.attribute_count);
    for (size_t i = 0; i < p_vertex_data.attribute_count; ++i) {
        const RUVertexView::Attribute& attribute = p_vertex_data.attributes.get()[i];
        result.attributes.push_back(VertexAttributeLayoutView{.role_name = attribute.role.get_name(),
            .role_hash = attribute.role.hash,
            .format = attribute.format,
            .offset = attribute.offset,
            .size = attribute.size});
    }
    return result;
}

static void validate_vertex_layout(const VertexLayoutView& p_vertex_layout) {
    size_t expected_offset = 0;
    for (size_t i = 0; i < p_vertex_layout.attributes.size(); ++i) {
        const VertexAttributeLayoutView& attribute = p_vertex_layout.attributes[i];
        if (attribute.role_name.empty() || attribute.role_name.contains('\0')) {
            throw std::runtime_error("Failed to resolve vertex input contract: resource attribute role is invalid.");
        }
        if (attribute.role_hash != dynam_hash(attribute.role_name)) {
            throw std::runtime_error(std::format(
                "Failed to resolve vertex input contract: resource role '{}' has an invalid hash.", attribute.role_name));
        }
        if (attribute.size != VertexAttributeFormatInfo::get_size(attribute.format) || attribute.offset != expected_offset ||
            attribute.size > std::numeric_limits<size_t>::max() - expected_offset) {
            throw std::runtime_error(std::format(
                "Failed to resolve vertex input contract: resource role '{}' has an invalid layout.", attribute.role_name));
        }
        for (size_t previous_index = 0; previous_index < i; ++previous_index) {
            const VertexAttributeLayoutView& previous = p_vertex_layout.attributes[previous_index];
            if (previous.role_name == attribute.role_name) {
                throw std::runtime_error(std::format(
                    "Failed to resolve vertex input contract: resource role '{}' is duplicated.", attribute.role_name));
            }
            if (previous.role_hash == attribute.role_hash) {
                throw std::runtime_error(std::format("Failed to resolve vertex input contract: distinct resource roles '{}' and '{}' collide at hash {}.",
                    previous.role_name,
                    attribute.role_name,
                    attribute.role_hash));
            }
        }
        expected_offset += attribute.size;
    }
    if (expected_offset != p_vertex_layout.vertex_stride) {
        throw std::runtime_error("Failed to resolve vertex input contract: resource attributes are not tightly packed to the declared stride.");
    }
}

static RUVertexAttributeFormat reflect_vertex_format(const ShaderMetadataTypeAtom& p_type, std::string_view p_field_name) {
    std::string_view scalar_type;
    int32_t component_count = 0;
    if (p_type.kind == "scalar") {
        scalar_type = p_type.scalar_type;
        component_count = 1;
    } else if (p_type.kind == "vector") {
        scalar_type = p_type.element_scalar_type;
        component_count = p_type.element_count;
    } else {
        throw std::runtime_error(std::format("Failed to resolve vertex input contract: WBE_VERTEX_INPUT field '{}' has unsupported shader type '{}'.",
            p_field_name,
            p_type.kind));
    }
    if (component_count < 1 || component_count > 4) {
        throw std::runtime_error(std::format("Failed to resolve vertex input contract: WBE_VERTEX_INPUT field '{}' has unsupported component count {}.",
            p_field_name,
            component_count));
    }
    const size_t index = static_cast<size_t>(component_count - 1);
    if (scalar_type == "float32") {
        constexpr RUVertexAttributeFormat FORMATS[] = {
            RUVertexAttributeFormat::FLOAT, RUVertexAttributeFormat::FLOAT2, RUVertexAttributeFormat::FLOAT3, RUVertexAttributeFormat::FLOAT4};
        return FORMATS[index];
    }
    if (scalar_type == "int32") {
        constexpr RUVertexAttributeFormat FORMATS[] = {
            RUVertexAttributeFormat::INT, RUVertexAttributeFormat::INT2, RUVertexAttributeFormat::INT3, RUVertexAttributeFormat::INT4};
        return FORMATS[index];
    }
    if (scalar_type == "uint32") {
        constexpr RUVertexAttributeFormat FORMATS[] = {
            RUVertexAttributeFormat::UINT, RUVertexAttributeFormat::UINT2, RUVertexAttributeFormat::UINT3, RUVertexAttributeFormat::UINT4};
        return FORMATS[index];
    }
    throw std::runtime_error(std::format(
        "Failed to resolve vertex input contract: WBE_VERTEX_INPUT field '{}' has unsupported scalar type '{}'.", p_field_name, scalar_type));
}

struct VertexAttributeMatch {
    const VertexAttributeLayoutView* attribute = nullptr;
    size_t source_binding = 0;
};

static VertexAttributeMatch find_vertex_attribute(std::span<const VertexLayoutView> p_vertex_layouts, const ShaderRole& p_role) {
    VertexAttributeMatch result;
    for (size_t binding_index = 0; binding_index < p_vertex_layouts.size(); ++binding_index) {
        for (const VertexAttributeLayoutView& attribute : p_vertex_layouts[binding_index].attributes) {
            if (!p_role.matches(attribute.role_name, attribute.role_hash)) {
                continue;
            }
            if (result.attribute != nullptr) {
                throw std::runtime_error(
                    std::format("Failed to resolve vertex input contract: resource role '{}' is duplicated.", p_role.get_name()));
            }
            result = VertexAttributeMatch{.attribute = &attribute, .source_binding = binding_index};
        }
    }
    if (result.attribute == nullptr) {
        throw std::runtime_error(
            std::format("Failed to resolve vertex input contract: resource role '{}' is missing.", p_role.get_name()));
    }
    return result;
}

// The layouts must already be validated.
static VertexInputContract match_vertex_input_requirement(
    const VertexInputRequirement& p_requirement, std::span<const VertexLayoutView> p_vertex_layouts) {
    VertexInputContract result;
    // The source binding of each matched attribute, parallel to result.attributes until bindings are compacted.
    std::vector<size_t> attribute_source_bindings;
    for (const VertexInputRequirement::Attribute& attribute : p_requirement.attributes) {
        const VertexAttributeMatch match = find_vertex_attribute(p_vertex_layouts, attribute.role);
        if (match.attribute->format != attribute.format) {
            throw std::runtime_error(std::format("Failed to resolve vertex input contract: shader role '{}' is incompatible with the resource format.",
                attribute.role.get_name()));
        }
        result.attributes.push_back(VertexInputContract::Attribute{
            .role = attribute.role, .format = attribute.format, .location = attribute.location, .offset = match.attribute->offset});
        attribute_source_bindings.push_back(match.source_binding);
    }
    // Keep only the bindings the shader reads, in source order.
    for (size_t source_binding = 0; source_binding < p_vertex_layouts.size(); ++source_binding) {
        if (std::ranges::find(attribute_source_bindings, source_binding) == attribute_source_bindings.end()) {
            continue;
        }
        const uint32_t binding = static_cast<uint32_t>(result.bindings.size());
        result.bindings.push_back(VertexInputContract::Binding{
            .source_binding = source_binding, .vertex_stride = p_vertex_layouts[source_binding].vertex_stride});
        for (size_t i = 0; i < result.attributes.size(); ++i) {
            if (attribute_source_bindings[i] == source_binding) {
                result.attributes[i].binding = binding;
            }
        }
    }
    std::sort(result.attributes.begin(), result.attributes.end(), [](const auto& p_left, const auto& p_right) {
        return p_left.location < p_right.location;
    });
    return result;
}

static std::vector<VertexLayoutView> make_validated_vertex_layout_views(std::span<const RUVertexView> p_vertex_bindings) {
    std::vector<VertexLayoutView> vertex_layouts;
    vertex_layouts.reserve(p_vertex_bindings.size());
    for (const RUVertexView& vertex_binding : p_vertex_bindings) {
        vertex_layouts.push_back(make_vertex_layout_view(vertex_binding));
        validate_vertex_layout(vertex_layouts.back());
    }
    return vertex_layouts;
}

VertexInputRequirement VertexInputContractResolver::resolve_requirement(const ShaderMetadata& p_metadata, std::string_view p_entry_name) const {
    const ShaderMetadataEntryPoint& entry_point = ShaderMetadataQuery().resolve_entry_point(p_metadata, "vertex", p_entry_name);
    ShaderRoleFieldResolver role_resolver;
    VertexInputRequirement result;
    for (const ShaderMetadataParameter& parameter : entry_point.parameters) {
        std::vector<ShaderRoleFieldResolver::Field> fields =
            role_resolver.collect_fields(parameter.type.fields, "WBE_VERTEX_INPUT", ShaderRoleFieldResolver::Traversal::RECURSIVE);
        for (ShaderRoleFieldResolver::Field& field : fields) {
            if (field.metadata->binding.kind != "varyingInput" || field.metadata->binding.index < 0) {
                throw std::runtime_error(std::format("Failed to resolve vertex input contract: WBE_VERTEX_INPUT field '{}' is not a located varying input.",
                    field.metadata->name));
            }
            const uint32_t location = static_cast<uint32_t>(field.metadata->binding.index);
            for (const VertexInputRequirement::Attribute& existing : result.attributes) {
                if (existing.role.get_name() == field.role.get_name()) {
                    throw std::runtime_error(std::format(
                        "Failed to resolve vertex input contract: shader role '{}' is duplicated.", field.role.get_name()));
                }
                if (existing.role.get_hash() == field.role.get_hash()) {
                    throw std::runtime_error(std::format("Failed to resolve vertex input contract: shader roles '{}' and '{}' collide at hash {}.",
                        existing.role.get_name(),
                        field.role.get_name(),
                        field.role.get_hash()));
                }
                if (existing.location == location) {
                    throw std::runtime_error(
                        std::format("Failed to resolve vertex input contract: shader location {} is duplicated.", location));
                }
            }
            const RUVertexAttributeFormat format = reflect_vertex_format(field.metadata->type, field.metadata->name);
            result.attributes.push_back(VertexInputRequirement::Attribute{.role = std::move(field.role), .format = format, .location = location});
        }
    }
    return result;
}

VertexInputContract VertexInputContractResolver::resolve(
    const VertexInputRequirement& p_requirement, std::span<const RUVertexView> p_vertex_bindings) const {
    return match_vertex_input_requirement(p_requirement, make_validated_vertex_layout_views(p_vertex_bindings));
}

VertexInputContract VertexInputContractResolver::resolve(
    const ShaderMetadata& p_metadata, std::string_view p_entry_name, std::span<const RUVertexView> p_vertex_bindings) const {
    // The resource layouts are validated before the shader, as before the requirement was split out.
    const std::vector<VertexLayoutView> vertex_layouts = make_validated_vertex_layout_views(p_vertex_bindings);
    return match_vertex_input_requirement(resolve_requirement(p_metadata, p_entry_name), vertex_layouts);
}

} // namespace WhiteBirdEngine

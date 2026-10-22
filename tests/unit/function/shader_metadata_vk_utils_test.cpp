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
#include "function/render/shader_metadata.hh"
#include "function/render/shader_role.hh"
#include "function/render/vertex_input_contract_resolver.hh"
#include "function/render/vertex_input_requirement.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <array>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

struct VertexAttributeTestSpec {
    std::string_view role;
    RUVertexAttributeFormat format;
    size_t offset;
    size_t size;
};

RefRes<char> push_vertex_test_data(StackAllocator& p_allocator, size_t p_size, const void* p_data) {
    MemID memory = p_allocator.allocate(p_size);
    if (p_size > 0) {
        std::memcpy(p_allocator.get(memory), p_data, p_size);
    }
    return RefRes<char>(memory, &p_allocator);
}

RUVertexView make_vertex_test_data(StackAllocator& p_allocator, std::span<const VertexAttributeTestSpec> p_specs, size_t p_stride) {
    std::vector<RUVertexView::Attribute> attributes;
    attributes.reserve(p_specs.size());
    for (const VertexAttributeTestSpec& spec : p_specs) {
        RefRes<char> role_name = push_vertex_test_data(p_allocator, spec.role.size() + 1, spec.role.data());
        role_name.get()[spec.role.size()] = '\0';
        RUShaderRole role;
        role.name = role_name;
        role.hash = dynam_hash(spec.role);
        attributes.push_back(RUVertexView::Attribute{.role = role, .format = spec.format, .offset = spec.offset, .size = spec.size});
    }
    RUVertexView result;
    if (!attributes.empty()) {
        result.attributes =
            push_vertex_test_data(p_allocator, attributes.size() * sizeof(attributes[0]), attributes.data()).force_cast<RUVertexView::Attribute>();
    }
    result.attribute_count = attributes.size();
    result.vertex_stride = p_stride;
    return result;
}

std::span<const RUVertexView> single_binding(const RUVertexView& p_vertex_data) {
    return {&p_vertex_data, 1};
}

ShaderMetadataTypeField make_vertex_shader_field(
    std::string_view p_name, std::string_view p_role, int32_t p_location, std::string_view p_kind, std::string_view p_scalar_type, int32_t p_component_count) {
    ShaderMetadataTypeField field;
    field.name = p_name;
    field.binding.kind = "varyingInput";
    field.binding.index = p_location;
    field.type.kind = p_kind;
    if (p_kind == "scalar") {
        field.type.scalar_type = p_scalar_type;
    } else {
        field.type.element_scalar_type = p_scalar_type;
        field.type.element_count = p_component_count;
    }
    ShaderMetadataUserAttribute attribute;
    attribute.name = "WBE_VERTEX_INPUT";
    if (!p_role.empty()) {
        ShaderMetadataUserAttributeArgument argument;
        argument.kind = "string";
        argument.string_value = p_role;
        attribute.arguments.push_back(argument);
    }
    field.user_attributes.push_back(attribute);
    return field;
}

ShaderMetadata make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField> p_fields) {
    ShaderMetadata metadata;
    ShaderMetadataEntryPoint entry_point;
    entry_point.name = "vertex_main";
    entry_point.stage = "vertex";
    ShaderMetadataParameter parameter;
    parameter.name = "p_input";
    parameter.type.kind = "struct";
    parameter.type.fields.assign(p_fields.begin(), p_fields.end());
    entry_point.parameters.push_back(parameter);
    metadata.entry_points.push_back(entry_point);
    return metadata;
}

ShaderMetadataTypeField make_descriptor_heap_field(std::string_view p_name,
    std::string_view p_descriptor_type,
    int32_t p_offset,
    std::string_view p_resource_id = {},
    std::string_view p_access = {}) {
    ShaderMetadataTypeField field;
    field.name = p_name;
    field.type.kind = "vector";
    field.type.element_scalar_type = "uint32";
    field.type.element_count = 2;
    field.binding.kind = "uniform";
    field.binding.offset = p_offset;
    field.binding.size = 8;
    field.binding.element_stride = 4;
    ShaderMetadataUserAttribute attribute;
    attribute.name = "WBE_DESCRIPTOR";
    ShaderMetadataUserAttributeArgument argument;
    argument.kind = "string";
    argument.string_value = p_descriptor_type;
    attribute.arguments.push_back(argument);
    argument.string_value = p_resource_id.empty() ? p_name : p_resource_id;
    attribute.arguments.push_back(argument);
    if (!p_access.empty()) {
        argument.string_value = p_access;
        attribute.arguments.push_back(argument);
    }
    field.user_attributes.push_back(attribute);
    return field;
}

ShaderMetadataTypeField make_default_push_field(
    std::string_view p_name, std::string_view p_value, std::string_view p_scalar_type, int32_t p_offset) {
    ShaderMetadataTypeField field;
    field.name = p_name;
    field.type.kind = "scalar";
    field.type.scalar_type = p_scalar_type;
    field.binding.kind = "uniform";
    field.binding.offset = p_offset;
    field.binding.size = 4;
    ShaderMetadataUserAttribute attribute;
    attribute.name = "WBE_PUSH_CONSTANT";
    ShaderMetadataUserAttributeArgument argument;
    argument.kind = "string";
    argument.string_value = p_value;
    attribute.arguments.push_back(argument);
    field.user_attributes.push_back(attribute);
    return field;
}

ShaderMetadata make_descriptor_heap_metadata(
    std::string_view p_stage, std::string_view p_entry_name, std::span<const ShaderMetadataTypeField> p_fields) {
    ShaderMetadata metadata;
    ShaderMetadataParameter push_data;
    push_data.name = "descriptor_indices";
    push_data.binding.kind = "pushConstantBuffer";
    push_data.type.kind = "constantBuffer";
    push_data.type.fields.assign(p_fields.begin(), p_fields.end());
    metadata.parameters.push_back(push_data);
    ShaderMetadataEntryPoint entry_point;
    entry_point.name = p_entry_name;
    entry_point.stage = p_stage;
    entry_point.bindings.push_back(push_data.binding);
    metadata.entry_points.push_back(entry_point);
    return metadata;
}

} // namespace

TEST(WBEShaderMetadataVKUtilsTest, CollectsExplicitDescriptorHeapUint2Fields) {
    ShaderMetadataTypeField images;
    images.name = "images";
    images.type.kind = "struct";
    images.binding.kind = "uniform";
    images.binding.offset = 8;
    images.binding.size = 16;
    images.fields.push_back(make_descriptor_heap_field("sampled", "sampled_image", 0));
    images.fields.push_back(make_descriptor_heap_field("storage", "storage_image", 8));
    const std::array<ShaderMetadataTypeField, 5> fields{{
        make_descriptor_heap_field("uniform", "uniform_buffer", 0),
        images,
        make_descriptor_heap_field("storage", "storage_buffer", 24),
        make_descriptor_heap_field("sampler", "sampler", 32),
        make_descriptor_heap_field("acceleration_structure", "acceleration_structure", 40),
    }};
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", fields);

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main");

    EXPECT_EQ(layout.size, 48U);
    EXPECT_EQ(layout.stage_flags, VK_SHADER_STAGE_COMPUTE_BIT);
    ASSERT_EQ(layout.fields.size(), 6U);
    EXPECT_EQ(layout.fields[0].field_path, "uniform");
    EXPECT_EQ(layout.fields[0].type, ShaderDescriptorHeapTypeVK::UNIFORM_BUFFER);
    EXPECT_EQ(layout.fields[0].resource_id, "uniform");
    EXPECT_EQ(layout.fields[0].offset, 0U);
    EXPECT_EQ(layout.fields[1].field_path, "images.sampled");
    EXPECT_EQ(layout.fields[1].type, ShaderDescriptorHeapTypeVK::SAMPLED_IMAGE);
    EXPECT_EQ(layout.fields[1].offset, 8U);
    EXPECT_EQ(layout.fields[2].field_path, "images.storage");
    EXPECT_EQ(layout.fields[2].type, ShaderDescriptorHeapTypeVK::STORAGE_IMAGE);
    EXPECT_EQ(layout.fields[2].offset, 16U);
    EXPECT_EQ(layout.fields[3].type, ShaderDescriptorHeapTypeVK::STORAGE_BUFFER);
    EXPECT_EQ(layout.fields[4].type, ShaderDescriptorHeapTypeVK::SAMPLER);
    EXPECT_EQ(layout.fields[5].field_path, "acceleration_structure");
    EXPECT_EQ(layout.fields[5].type, ShaderDescriptorHeapTypeVK::ACCELERATION_STRUCTURE);
    EXPECT_EQ(layout.fields[5].offset, 40U);
}

TEST(WBEShaderMetadataVKUtilsTest, CollectsRendererDefaultPushFields) {
    const std::array<ShaderMetadataTypeField, 2> fields{{
        make_default_push_field("elapsed", "time", "float32", 0),
        make_default_push_field("frame_slot", "frame_slot", "uint32", 4),
    }};
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", fields);

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main");

    EXPECT_EQ(layout.size, 8U);
    EXPECT_TRUE(layout.fields.empty());
    ASSERT_EQ(layout.default_fields.size(), 2U);
    EXPECT_EQ(layout.default_fields[0].field_path, "elapsed");
    EXPECT_EQ(layout.default_fields[0].value, ShaderDefaultPushValueVK::TIME);
    EXPECT_EQ(layout.default_fields[0].offset, 0U);
    EXPECT_EQ(layout.default_fields[1].field_path, "frame_slot");
    EXPECT_EQ(layout.default_fields[1].value, ShaderDefaultPushValueVK::FRAME_SLOT);
    EXPECT_EQ(layout.default_fields[1].offset, 4U);
}

TEST(WBEShaderMetadataVKUtilsTest, CollectsCustomPushConstantFields) {
    const std::array<ShaderMetadataTypeField, 4> fields{{
        make_default_push_field("level", "level", "uint32", 0),
        make_default_push_field("offset", "offset", "int32", 4),
        make_default_push_field("scale", "scale", "float32", 8),
        make_default_push_field("frame_slot", "frame_slot", "uint32", 12),
    }};
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", fields);

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main");

    EXPECT_EQ(layout.size, 16U);
    ASSERT_EQ(layout.default_fields.size(), 4U);
    EXPECT_EQ(layout.default_fields[0].value, ShaderDefaultPushValueVK::CUSTOM);
    EXPECT_EQ(layout.default_fields[0].name, "level");
    EXPECT_EQ(layout.default_fields[0].type, ShaderPushConstantTypeVK::UINT32);
    EXPECT_EQ(layout.default_fields[1].value, ShaderDefaultPushValueVK::CUSTOM);
    EXPECT_EQ(layout.default_fields[1].type, ShaderPushConstantTypeVK::INT32);
    EXPECT_EQ(layout.default_fields[1].offset, 4U);
    EXPECT_EQ(layout.default_fields[2].value, ShaderDefaultPushValueVK::CUSTOM);
    EXPECT_EQ(layout.default_fields[2].name, "scale");
    EXPECT_EQ(layout.default_fields[2].type, ShaderPushConstantTypeVK::FLOAT32);
    EXPECT_EQ(layout.default_fields[3].value, ShaderDefaultPushValueVK::FRAME_SLOT);
    EXPECT_TRUE(layout.default_fields[3].name.empty());
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsMalformedRendererDefaultPushFields) {
    ShaderMetadataTypeField malformed = make_default_push_field("value", "custom", "float64", 0);
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_default_push_field("value", "time", "uint32", 0);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_default_push_field("value", "time", "bool", 0);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_default_push_field("value", "frame_slot", "float32", 0);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_default_push_field("value", "time", "float32", 0);
    malformed.user_attributes[0].arguments.clear();
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_default_push_field("value", "time", "float32", 0);
    malformed.user_attributes.push_back(malformed.user_attributes[0]);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsOverlappingDescriptorAndDefaultPushFields) {
    const std::array<ShaderMetadataTypeField, 2> fields{{
        make_descriptor_heap_field("descriptor", "uniform_buffer", 0),
        make_default_push_field("time", "time", "float32", 4),
    }};
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", fields);

    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, ExpandsDescriptorStructArraysIntoExplicitPaths) {
    ShaderMetadataTypeField lights;
    lights.name = "lights";
    lights.type.kind = "array";
    lights.type.element_count = 2;
    lights.binding.kind = "uniform";
    lights.binding.offset = 8;
    lights.binding.size = 32;
    lights.binding.element_stride = 16;
    lights.fields.push_back(make_descriptor_heap_field("info", "uniform_buffer", 0));
    lights.fields.push_back(make_descriptor_heap_field("shadowmap", "sampled_image", 8));
    ShaderMetadata metadata = make_descriptor_heap_metadata("fragment", "fragment_main", std::span(&lights, 1));

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "fragment", "fragment_main");

    EXPECT_EQ(layout.size, 40U);
    ASSERT_EQ(layout.fields.size(), 4U);
    EXPECT_EQ(layout.fields[0].field_path, "lights[0].info");
    EXPECT_EQ(layout.fields[0].offset, 8U);
    EXPECT_EQ(layout.fields[1].field_path, "lights[0].shadowmap");
    EXPECT_EQ(layout.fields[1].offset, 16U);
    EXPECT_EQ(layout.fields[2].field_path, "lights[1].info");
    EXPECT_EQ(layout.fields[2].offset, 24U);
    EXPECT_EQ(layout.fields[3].field_path, "lights[1].shadowmap");
    EXPECT_EQ(layout.fields[3].offset, 32U);
}

TEST(WBEShaderMetadataVKUtilsTest, ResolvesIndexedResourceIdsPerDescriptorStructArrayElement) {
    ShaderMetadataTypeField shadow;
    shadow.name = "shadow";
    shadow.type.kind = "struct";
    shadow.binding.kind = "uniform";
    shadow.binding.offset = 8;
    shadow.binding.size = 8;
    shadow.fields.push_back(make_descriptor_heap_field("moments", "sampled_image", 0, "shadow_moments[]"));
    ShaderMetadataTypeField lights;
    lights.name = "lights";
    lights.type.kind = "array";
    lights.type.element_count = 2;
    lights.binding.kind = "uniform";
    lights.binding.offset = 0;
    lights.binding.size = 48;
    lights.binding.element_stride = 24;
    lights.fields.push_back(make_descriptor_heap_field("shadowmap", "sampled_image", 0, "shadowmap[]"));
    lights.fields.push_back(shadow);
    lights.fields.push_back(make_descriptor_heap_field("sampler", "sampler", 16, "shadow_sampler"));
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&lights, 1));

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main");

    ASSERT_EQ(layout.fields.size(), 6U);
    EXPECT_EQ(layout.fields[0].resource_id, "shadowmap[0]");
    // Nested non-array structs use the index of the enclosing array element.
    EXPECT_EQ(layout.fields[1].field_path, "lights[0].shadow.moments");
    EXPECT_EQ(layout.fields[1].resource_id, "shadow_moments[0]");
    // Resource ids without the suffix stay shared by every element.
    EXPECT_EQ(layout.fields[2].resource_id, "shadow_sampler");
    EXPECT_EQ(layout.fields[3].resource_id, "shadowmap[1]");
    EXPECT_EQ(layout.fields[4].resource_id, "shadow_moments[1]");
    EXPECT_EQ(layout.fields[5].resource_id, "shadow_sampler");
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsIndexedResourceIdOutsideDescriptorStructArray) {
    ShaderMetadataTypeField field = make_descriptor_heap_field("shadowmap", "sampled_image", 0, "shadowmap[]");
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&field, 1));

    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsIndexedResourceIdWithoutName) {
    ShaderMetadataTypeField lights;
    lights.name = "lights";
    lights.type.kind = "array";
    lights.type.element_count = 1;
    lights.binding.kind = "uniform";
    lights.binding.size = 8;
    lights.binding.element_stride = 8;
    lights.fields.push_back(make_descriptor_heap_field("shadowmap", "sampled_image", 0, "[]"));
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&lights, 1));

    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, ReturnsEmptyLayoutForDescriptorFreeEntryPoint) {
    ShaderMetadata metadata;
    ShaderMetadataEntryPoint entry_point;
    entry_point.name = "compute_main";
    entry_point.stage = "compute";
    metadata.entry_points.push_back(entry_point);

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main");

    EXPECT_EQ(layout.size, 0U);
    EXPECT_EQ(layout.stage_flags, 0U);
    EXPECT_TRUE(layout.fields.empty());
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsMalformedDescriptorHeapFields) {
    ShaderMetadataTypeField malformed = make_descriptor_heap_field("value", "sampled_image", 0);
    malformed.type.element_count = 1;
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_descriptor_heap_field("value", "unknown", 0);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_descriptor_heap_field("value", "sampler", 4);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_descriptor_heap_field("value", "sampler", 0);
    malformed.user_attributes[0].arguments.clear();
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_descriptor_heap_field("value", "sampler", 0);
    malformed.user_attributes[0].arguments[1].string_value.clear();
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);

    malformed = make_descriptor_heap_field("value", "sampler", 0);
    malformed.user_attributes.push_back(malformed.user_attributes[0]);
    metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&malformed, 1));
    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsMultiplePushDataParameters) {
    ShaderMetadataTypeField field = make_descriptor_heap_field("value", "uniform_buffer", 0);
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", std::span(&field, 1));
    metadata.parameters.push_back(metadata.parameters[0]);

    EXPECT_THROW(collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main"), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, MergesGraphicsDescriptorHeapLayouts) {
    ShaderMetadataTypeField uniform = make_descriptor_heap_field("uniform", "uniform_buffer", 0);
    ShaderMetadata vertex_metadata = make_descriptor_heap_metadata("vertex", "vertex_main", std::span(&uniform, 1));
    ShaderMetadata fragment_metadata = make_descriptor_heap_metadata("fragment", "fragment_main", std::span(&uniform, 1));
    ShaderMetadataTypeField sampler = make_descriptor_heap_field("sampler", "sampler", 8);
    ShaderMetadata geometry_metadata = make_descriptor_heap_metadata("geometry", "geometry_main", std::span(&sampler, 1));

    ShaderDescriptorHeapLayoutVK layout = collect_graphics_pipeline_descriptor_heap_layout(
        vertex_metadata, "vertex_main", &geometry_metadata, "geometry_main", fragment_metadata, "fragment_main");

    EXPECT_EQ(layout.size, 16U);
    EXPECT_EQ(layout.stage_flags, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    ASSERT_EQ(layout.fields.size(), 2U);
    EXPECT_EQ(layout.fields[0].field_path, "uniform");
    EXPECT_EQ(layout.fields[1].field_path, "sampler");

    fragment_metadata.parameters[0].type.fields[0].type.element_count = 1;
    EXPECT_THROW(collect_graphics_pipeline_descriptor_heap_layout(vertex_metadata, "vertex_main", nullptr, {}, fragment_metadata, "fragment_main"),
        std::runtime_error);

    ShaderMetadataTypeField mismatched_resource = make_descriptor_heap_field("uniform", "uniform_buffer", 0, "other");
    fragment_metadata = make_descriptor_heap_metadata("fragment", "fragment_main", std::span(&mismatched_resource, 1));
    EXPECT_THROW(collect_graphics_pipeline_descriptor_heap_layout(vertex_metadata, "vertex_main", nullptr, {}, fragment_metadata, "fragment_main"),
        std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, MergesGraphicsRendererDefaultPushFields) {
    ShaderMetadataTypeField time = make_default_push_field("time", "time", "float32", 0);
    ShaderMetadata vertex_metadata = make_descriptor_heap_metadata("vertex", "vertex_main", std::span(&time, 1));
    ShaderMetadata fragment_metadata = make_descriptor_heap_metadata("fragment", "fragment_main", std::span(&time, 1));
    ShaderMetadata geometry_metadata = make_descriptor_heap_metadata("geometry", "geometry_main", std::span(&time, 1));

    ShaderDescriptorHeapLayoutVK layout = collect_graphics_pipeline_descriptor_heap_layout(
        vertex_metadata, "vertex_main", &geometry_metadata, "geometry_main", fragment_metadata, "fragment_main");

    EXPECT_EQ(layout.size, 4U);
    ASSERT_EQ(layout.default_fields.size(), 1U);
    EXPECT_EQ(layout.default_fields[0].value, ShaderDefaultPushValueVK::TIME);

    fragment_metadata.parameters[0].type.fields[0].type.scalar_type = "bool";
    EXPECT_THROW(collect_graphics_pipeline_descriptor_heap_layout(vertex_metadata, "vertex_main", nullptr, {}, fragment_metadata, "fragment_main"),
        std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, AllowsMultipleFieldsToUseOneResourceId) {
    const std::array<ShaderMetadataTypeField, 2> fields{{
        make_descriptor_heap_field("first", "uniform_buffer", 0, "shared"),
        make_descriptor_heap_field("second", "uniform_buffer", 8, "shared"),
    }};
    ShaderDescriptorHeapLayoutVK layout =
        collect_shader_descriptor_heap_layout(make_descriptor_heap_metadata("compute", "main", fields), "compute", "main");
    ASSERT_EQ(layout.fields.size(), 2U);
    EXPECT_EQ(layout.fields[0].resource_id, "shared");
    EXPECT_EQ(layout.fields[1].resource_id, "shared");
}

TEST(WBEShaderMetadataVKUtilsTest, BuildsEverySupportedDynamicVertexInputFormat) {
    struct FormatCase {
        RUVertexAttributeFormat resource_format;
        std::string_view kind;
        std::string_view scalar_type;
        int32_t component_count;
        size_t size;
        VkFormat vk_format;
    };
    const std::array<FormatCase, 12> cases{{
        {.resource_format = RUVertexAttributeFormat::FLOAT, .kind = "scalar", .scalar_type = "float32", .component_count = 1, .size = 4, .vk_format = VK_FORMAT_R32_SFLOAT},
        {.resource_format = RUVertexAttributeFormat::FLOAT2, .kind = "vector", .scalar_type = "float32", .component_count = 2, .size = 8, .vk_format = VK_FORMAT_R32G32_SFLOAT},
        {.resource_format = RUVertexAttributeFormat::FLOAT3, .kind = "vector", .scalar_type = "float32", .component_count = 3, .size = 12, .vk_format = VK_FORMAT_R32G32B32_SFLOAT},
        {.resource_format = RUVertexAttributeFormat::FLOAT4, .kind = "vector", .scalar_type = "float32", .component_count = 4, .size = 16, .vk_format = VK_FORMAT_R32G32B32A32_SFLOAT},
        {.resource_format = RUVertexAttributeFormat::INT, .kind = "scalar", .scalar_type = "int32", .component_count = 1, .size = 4, .vk_format = VK_FORMAT_R32_SINT},
        {.resource_format = RUVertexAttributeFormat::INT2, .kind = "vector", .scalar_type = "int32", .component_count = 2, .size = 8, .vk_format = VK_FORMAT_R32G32_SINT},
        {.resource_format = RUVertexAttributeFormat::INT3, .kind = "vector", .scalar_type = "int32", .component_count = 3, .size = 12, .vk_format = VK_FORMAT_R32G32B32_SINT},
        {.resource_format = RUVertexAttributeFormat::INT4, .kind = "vector", .scalar_type = "int32", .component_count = 4, .size = 16, .vk_format = VK_FORMAT_R32G32B32A32_SINT},
        {.resource_format = RUVertexAttributeFormat::UINT, .kind = "scalar", .scalar_type = "uint32", .component_count = 1, .size = 4, .vk_format = VK_FORMAT_R32_UINT},
        {.resource_format = RUVertexAttributeFormat::UINT2, .kind = "vector", .scalar_type = "uint32", .component_count = 2, .size = 8, .vk_format = VK_FORMAT_R32G32_UINT},
        {.resource_format = RUVertexAttributeFormat::UINT3, .kind = "vector", .scalar_type = "uint32", .component_count = 3, .size = 12, .vk_format = VK_FORMAT_R32G32B32_UINT},
        {.resource_format = RUVertexAttributeFormat::UINT4, .kind = "vector", .scalar_type = "uint32", .component_count = 4, .size = 16, .vk_format = VK_FORMAT_R32G32B32A32_UINT},
    }};
    for (const FormatCase& format_case : cases) {
        StackAllocator allocator(WBE_KI_B(8));
        const VertexAttributeTestSpec spec{
            .role = "value", .format = format_case.resource_format, .offset = 0, .size = format_case.size};
        RUVertexView vertex_data =
            make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&spec, 1), format_case.size);
        const ShaderMetadataTypeField field =
            make_vertex_shader_field("value", "", 3, format_case.kind, format_case.scalar_type, format_case.component_count);
        ShaderMetadata metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&field, 1));

        VertexInputDescriptionVK description = build_vertex_input_description_vk(metadata, "vertex_main", single_binding(vertex_data));
        ASSERT_EQ(description.bindings.size(), 1U);
        ASSERT_EQ(description.attributes.size(), 1U);
        EXPECT_EQ(description.bindings[0].stride, format_case.size);
        EXPECT_EQ(description.attributes[0].location, 3U);
        EXPECT_EQ(description.attributes[0].format, format_case.vk_format);
    }
}

TEST(WBEShaderMetadataVKUtilsTest, MatchesShaderSubsetAndAllowsExtraResourceAttributes) {
    StackAllocator allocator(WBE_KI_B(8));
    const std::array<VertexAttributeTestSpec, 2> specs{{
        {.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12},
        {.role = "uv", .format = RUVertexAttributeFormat::FLOAT2, .offset = 12, .size = 8},
    }};
    RUVertexView vertex_data = make_vertex_test_data(allocator, specs, 20);
    const ShaderMetadataTypeField field = make_vertex_shader_field("input_position", "position", 5, "vector", "float32", 3);
    ShaderMetadata metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&field, 1));

    VertexInputDescriptionVK description = build_vertex_input_description_vk(metadata, "vertex_main", single_binding(vertex_data));
    ASSERT_EQ(description.bindings.size(), 1U);
    ASSERT_EQ(description.attributes.size(), 1U);
    EXPECT_EQ(description.bindings[0].stride, 20U);
    EXPECT_EQ(description.attributes[0].location, 5U);
    EXPECT_EQ(description.attributes[0].offset, 0U);
}

TEST(WBEShaderMetadataVKUtilsTest, FindsAnnotatedFieldsInNestedVertexStructs) {
    StackAllocator allocator(WBE_KI_B(8));
    const VertexAttributeTestSpec spec{.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12};
    RUVertexView vertex_data = make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&spec, 1), 12);
    ShaderMetadataTypeField position = make_vertex_shader_field("position", "", 2, "vector", "float32", 3);
    ShaderMetadataTypeField nested;
    nested.name = "nested";
    nested.type.kind = "struct";
    nested.fields.push_back(position);
    ShaderMetadata metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&nested, 1));

    VertexInputDescriptionVK description = build_vertex_input_description_vk(metadata, "vertex_main", single_binding(vertex_data));

    ASSERT_EQ(description.attributes.size(), 1U);
    EXPECT_EQ(description.attributes[0].location, 2U);
    EXPECT_EQ(description.attributes[0].format, VK_FORMAT_R32G32B32_SFLOAT);
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsInvalidVertexInputContracts) {
    StackAllocator allocator(WBE_KI_B(32));
    const std::array<VertexAttributeTestSpec, 2> specs{{
        {.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12},
        {.role = "uv", .format = RUVertexAttributeFormat::FLOAT2, .offset = 12, .size = 8},
    }};
    RUVertexView vertex_data = make_vertex_test_data(allocator, specs, 20);

    ShaderMetadataTypeField missing = make_vertex_shader_field("missing", "missing", 0, "vector", "float32", 3);
    ShaderMetadata missing_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&missing, 1));
    EXPECT_THROW(build_vertex_input_description_vk(missing_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    ShaderMetadataTypeField incompatible = make_vertex_shader_field("position", "", 0, "vector", "uint32", 3);
    ShaderMetadata incompatible_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&incompatible, 1));
    EXPECT_THROW(build_vertex_input_description_vk(incompatible_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    std::array<ShaderMetadataTypeField, 2> duplicate_role_fields{{
        make_vertex_shader_field("first", "position", 0, "vector", "float32", 3),
        make_vertex_shader_field("second", "position", 1, "vector", "float32", 3),
    }};
    ShaderMetadata duplicate_role_metadata = make_vertex_shader_metadata(duplicate_role_fields);
    EXPECT_THROW(build_vertex_input_description_vk(duplicate_role_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    std::array<ShaderMetadataTypeField, 2> duplicate_location_fields{{
        make_vertex_shader_field("position", "", 0, "vector", "float32", 3),
        make_vertex_shader_field("uv", "", 0, "vector", "float32", 2),
    }};
    ShaderMetadata duplicate_location_metadata = make_vertex_shader_metadata(duplicate_location_fields);
    EXPECT_THROW(build_vertex_input_description_vk(duplicate_location_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    ShaderMetadataTypeField malformed = make_vertex_shader_field("position", "", 0, "vector", "float32", 3);
    malformed.user_attributes[0].arguments.emplace_back();
    malformed.user_attributes[0].arguments[0].kind = "int";
    ShaderMetadata malformed_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&malformed, 1));
    EXPECT_THROW(build_vertex_input_description_vk(malformed_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    ShaderMetadataTypeField repeated = make_vertex_shader_field("position", "", 0, "vector", "float32", 3);
    repeated.user_attributes.push_back(repeated.user_attributes[0]);
    ShaderMetadata repeated_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&repeated, 1));
    EXPECT_THROW(build_vertex_input_description_vk(repeated_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    ShaderMetadataTypeField unsupported = make_vertex_shader_field("position", "", 0, "matrix", "float32", 3);
    ShaderMetadata unsupported_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&unsupported, 1));
    EXPECT_THROW(build_vertex_input_description_vk(unsupported_metadata, "vertex_main", single_binding(vertex_data)), std::runtime_error);

    const std::array<VertexAttributeTestSpec, 2> duplicate_specs{{
        {.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12},
        {.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 12, .size = 12},
    }};
    RUVertexView duplicate_resource = make_vertex_test_data(allocator, duplicate_specs, 24);
    ShaderMetadataTypeField position = make_vertex_shader_field("position", "", 0, "vector", "float32", 3);
    ShaderMetadata position_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&position, 1));
    EXPECT_THROW(build_vertex_input_description_vk(position_metadata, "vertex_main", single_binding(duplicate_resource)), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, MatchesRolesAcrossVertexBindingsAndKeepsOnlyReadBindings) {
    StackAllocator allocator(WBE_KI_B(16));
    const VertexAttributeTestSpec position_spec{.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12};
    const VertexAttributeTestSpec color_spec{.role = "color", .format = RUVertexAttributeFormat::FLOAT4, .offset = 0, .size = 16};
    const std::array<VertexAttributeTestSpec, 2> joint_specs{{
        {.role = "joint_binding_ids", .format = RUVertexAttributeFormat::INT4, .offset = 0, .size = 16},
        {.role = "joint_binding_weights", .format = RUVertexAttributeFormat::FLOAT4, .offset = 16, .size = 16},
    }};
    const std::array<RUVertexView, 3> bindings{
        make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&position_spec, 1), 12),
        make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&color_spec, 1), 16),
        make_vertex_test_data(allocator, joint_specs, 32),
    };
    // The shader reads the first and last bindings but not the color binding.
    const std::array<ShaderMetadataTypeField, 3> fields{{
        make_vertex_shader_field("weights", "joint_binding_weights", 2, "vector", "float32", 4),
        make_vertex_shader_field("position", "", 0, "vector", "float32", 3),
        make_vertex_shader_field("ids", "joint_binding_ids", 1, "vector", "int32", 4),
    }};
    ShaderMetadata metadata = make_vertex_shader_metadata(fields);

    VertexInputDescriptionVK description = build_vertex_input_description_vk(metadata, "vertex_main", bindings);

    ASSERT_EQ(description.bindings.size(), 2U);
    EXPECT_EQ(description.bindings[0].binding, 0U);
    EXPECT_EQ(description.bindings[0].stride, 12U);
    EXPECT_EQ(description.bindings[1].binding, 1U);
    EXPECT_EQ(description.bindings[1].stride, 32U);
    EXPECT_EQ(description.source_bindings, (std::vector<size_t>{0, 2}));
    ASSERT_EQ(description.attributes.size(), 3U);
    EXPECT_EQ(description.attributes[0].location, 0U);
    EXPECT_EQ(description.attributes[0].binding, 0U);
    EXPECT_EQ(description.attributes[1].location, 1U);
    EXPECT_EQ(description.attributes[1].binding, 1U);
    EXPECT_EQ(description.attributes[1].format, VK_FORMAT_R32G32B32A32_SINT);
    EXPECT_EQ(description.attributes[1].offset, 0U);
    EXPECT_EQ(description.attributes[2].location, 2U);
    EXPECT_EQ(description.attributes[2].binding, 1U);
    EXPECT_EQ(description.attributes[2].offset, 16U);

    const std::array<RUVertexView, 2> duplicated_bindings{
        make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&position_spec, 1), 12),
        make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&position_spec, 1), 12),
    };
    ShaderMetadata position_metadata = make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&fields[1], 1));
    EXPECT_THROW(build_vertex_input_description_vk(position_metadata, "vertex_main", duplicated_bindings), std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, BuildsEmptyStateForVertexlessEntry) {
    StackAllocator allocator(WBE_KI_B(4));
    RUVertexView vertex_data = make_vertex_test_data(allocator, {}, 0);
    ShaderMetadata metadata = make_vertex_shader_metadata({});
    VertexInputDescriptionVK description = build_vertex_input_description_vk(metadata, "vertex_main", single_binding(vertex_data));
    EXPECT_TRUE(description.bindings.empty());
    EXPECT_TRUE(description.attributes.empty());
}

TEST(WBEShaderMetadataVKUtilsTest, ReflectsDescriptorAccessFromTheOptionalThirdArgument) {
    const std::array<ShaderMetadataTypeField, 6> fields{{
        make_descriptor_heap_field("uniform", "uniform_buffer", 0),
        make_descriptor_heap_field("storage_default", "storage_buffer", 8),
        make_descriptor_heap_field("storage_read", "storage_buffer", 16, {}, "read"),
        make_descriptor_heap_field("image_write", "storage_image", 24, {}, "write"),
        make_descriptor_heap_field("image_read_write", "storage_image", 32, {}, "read_write"),
        make_descriptor_heap_field("sampled_read", "sampled_image", 40, {}, "read"),
    }};
    ShaderMetadata metadata = make_descriptor_heap_metadata("compute", "compute_main", fields);

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(metadata, "compute", "compute_main");

    ASSERT_EQ(layout.fields.size(), 6U);
    EXPECT_EQ(layout.fields[0].access, ShaderDescriptorAccessVK::READ);
    EXPECT_EQ(layout.fields[1].access, ShaderDescriptorAccessVK::READ_WRITE);
    EXPECT_EQ(layout.fields[2].access, ShaderDescriptorAccessVK::READ);
    EXPECT_EQ(layout.fields[3].access, ShaderDescriptorAccessVK::WRITE);
    EXPECT_EQ(layout.fields[4].access, ShaderDescriptorAccessVK::READ_WRITE);
    EXPECT_EQ(layout.fields[5].access, ShaderDescriptorAccessVK::READ);
}

TEST(WBEShaderMetadataVKUtilsTest, TreatsTheReflectedEmptyAccessDefaultAsOmitted) {
    ShaderMetadataTypeField storage = make_descriptor_heap_field("storage", "storage_buffer", 0);
    ShaderMetadataTypeField uniform = make_descriptor_heap_field("uniform", "uniform_buffer", 8);
    for (ShaderMetadataTypeField* field : {&storage, &uniform}) {
        ShaderMetadataUserAttributeArgument empty_access;
        empty_access.kind = "string";
        field->user_attributes[0].arguments.push_back(empty_access);
    }
    const std::array<ShaderMetadataTypeField, 2> fields{{storage, uniform}};

    ShaderDescriptorHeapLayoutVK layout = collect_shader_descriptor_heap_layout(
        make_descriptor_heap_metadata("compute", "compute_main", fields), "compute", "compute_main");

    ASSERT_EQ(layout.fields.size(), 2U);
    EXPECT_EQ(layout.fields[0].access, ShaderDescriptorAccessVK::READ_WRITE);
    EXPECT_EQ(layout.fields[1].access, ShaderDescriptorAccessVK::READ);
}

TEST(WBEShaderMetadataVKUtilsTest, RejectsInvalidDescriptorAccess) {
    const std::array<ShaderMetadataTypeField, 1> unknown{{make_descriptor_heap_field("storage", "storage_buffer", 0, {}, "modify")}};
    EXPECT_THROW(collect_shader_descriptor_heap_layout(
                     make_descriptor_heap_metadata("compute", "compute_main", unknown), "compute", "compute_main"),
        std::runtime_error);
    const std::array<ShaderMetadataTypeField, 1> read_only_write{
        {make_descriptor_heap_field("sampled", "sampled_image", 0, {}, "write")}};
    EXPECT_THROW(collect_shader_descriptor_heap_layout(
                     make_descriptor_heap_metadata("compute", "compute_main", read_only_write), "compute", "compute_main"),
        std::runtime_error);
}

TEST(WBEShaderMetadataVKUtilsTest, MergesDescriptorAccessesAcrossGraphicsStages) {
    const std::array<ShaderMetadataTypeField, 1> vertex_fields{{make_descriptor_heap_field("state", "storage_buffer", 0, {}, "read")}};
    const std::array<ShaderMetadataTypeField, 1> fragment_fields{
        {make_descriptor_heap_field("state", "storage_buffer", 0, {}, "write")}};
    ShaderMetadata vertex = make_descriptor_heap_metadata("vertex", "vertex_main", vertex_fields);
    ShaderMetadata fragment = make_descriptor_heap_metadata("fragment", "fragment_main", fragment_fields);

    ShaderDescriptorHeapLayoutVK layout =
        collect_graphics_pipeline_descriptor_heap_layout(vertex, "vertex_main", nullptr, {}, fragment, "fragment_main");

    ASSERT_EQ(layout.fields.size(), 1U);
    EXPECT_EQ(layout.fields[0].access, ShaderDescriptorAccessVK::READ_WRITE);
}

TEST(WBEShaderMetadataVKUtilsTest, VertexInputRequirementMatchesLikeTheShaderMetadata) {
    StackAllocator allocator(WBE_KI_B(16));
    const VertexAttributeTestSpec position_spec{.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12};
    const std::array<VertexAttributeTestSpec, 2> joint_specs{{
        {.role = "joint_binding_ids", .format = RUVertexAttributeFormat::INT4, .offset = 0, .size = 16},
        {.role = "joint_binding_weights", .format = RUVertexAttributeFormat::FLOAT4, .offset = 16, .size = 16},
    }};
    const std::array<RUVertexView, 2> bindings{
        make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&position_spec, 1), 12),
        make_vertex_test_data(allocator, joint_specs, 32),
    };
    const std::array<ShaderMetadataTypeField, 2> fields{{
        make_vertex_shader_field("weights", "joint_binding_weights", 1, "vector", "float32", 4),
        make_vertex_shader_field("position", "", 0, "vector", "float32", 3),
    }};
    const ShaderMetadata metadata = make_vertex_shader_metadata(fields);

    // The requirement needs no mesh and keeps the shader's reflection order.
    const VertexInputRequirement requirement = VertexInputContractResolver().resolve_requirement(metadata, "vertex_main");
    ASSERT_EQ(requirement.attributes.size(), 2U);
    EXPECT_EQ(requirement.attributes[0].role.get_name(), "joint_binding_weights");
    EXPECT_EQ(requirement.attributes[0].format, RUVertexAttributeFormat::FLOAT4);
    EXPECT_EQ(requirement.attributes[0].location, 1U);
    EXPECT_EQ(requirement.attributes[1].role.get_name(), "position");
    EXPECT_EQ(requirement.attributes[1].location, 0U);

    const VertexInputDescriptionVK from_metadata = build_vertex_input_description_vk(metadata, "vertex_main", bindings);
    const VertexInputDescriptionVK from_requirement = build_vertex_input_description_vk(requirement, bindings);
    EXPECT_EQ(from_requirement.source_bindings, from_metadata.source_bindings);
    ASSERT_EQ(from_requirement.bindings.size(), from_metadata.bindings.size());
    for (size_t i = 0; i < from_metadata.bindings.size(); ++i) {
        EXPECT_EQ(from_requirement.bindings[i].binding, from_metadata.bindings[i].binding);
        EXPECT_EQ(from_requirement.bindings[i].stride, from_metadata.bindings[i].stride);
    }
    ASSERT_EQ(from_requirement.attributes.size(), from_metadata.attributes.size());
    for (size_t i = 0; i < from_metadata.attributes.size(); ++i) {
        EXPECT_EQ(from_requirement.attributes[i].location, from_metadata.attributes[i].location);
        EXPECT_EQ(from_requirement.attributes[i].binding, from_metadata.attributes[i].binding);
        EXPECT_EQ(from_requirement.attributes[i].format, from_metadata.attributes[i].format);
        EXPECT_EQ(from_requirement.attributes[i].offset, from_metadata.attributes[i].offset);
    }
}

TEST(WBEShaderMetadataVKUtilsTest, VertexInputRequirementSeparatesShaderAndMeshErrors) {
    std::array<ShaderMetadataTypeField, 2> duplicate_location_fields{{
        make_vertex_shader_field("position", "", 0, "vector", "float32", 3),
        make_vertex_shader_field("uv", "", 0, "vector", "float32", 2),
    }};
    EXPECT_THROW(
        (void)VertexInputContractResolver().resolve_requirement(make_vertex_shader_metadata(duplicate_location_fields), "vertex_main"),
        std::runtime_error);
    ShaderMetadataTypeField unsupported = make_vertex_shader_field("position", "", 0, "matrix", "float32", 3);
    EXPECT_THROW((void)VertexInputContractResolver().resolve_requirement(
                     make_vertex_shader_metadata(std::span<const ShaderMetadataTypeField>(&unsupported, 1)), "vertex_main"),
        std::runtime_error);

    StackAllocator allocator(WBE_KI_B(8));
    const VertexAttributeTestSpec position_spec{.role = "position", .format = RUVertexAttributeFormat::FLOAT3, .offset = 0, .size = 12};
    const RUVertexView vertex_data = make_vertex_test_data(allocator, std::span<const VertexAttributeTestSpec>(&position_spec, 1), 12);
    VertexInputRequirement missing;
    missing.attributes.push_back(VertexInputRequirement::Attribute{.role = ShaderRole("uv"), .format = RUVertexAttributeFormat::FLOAT2, .location = 0});
    EXPECT_THROW((void)build_vertex_input_description_vk(missing, single_binding(vertex_data)), std::runtime_error);
    VertexInputRequirement incompatible;
    incompatible.attributes.push_back(
        VertexInputRequirement::Attribute{.role = ShaderRole("position"), .format = RUVertexAttributeFormat::UINT3, .location = 0});
    EXPECT_THROW((void)build_vertex_input_description_vk(incompatible, single_binding(vertex_data)), std::runtime_error);

    // An empty requirement is a vertex-less draw, whatever the mesh declares.
    const VertexInputDescriptionVK vertex_less = build_vertex_input_description_vk(VertexInputRequirement{}, single_binding(vertex_data));
    EXPECT_TRUE(vertex_less.bindings.empty());
    EXPECT_TRUE(vertex_less.attributes.empty());
}

} // namespace WhiteBirdEngine

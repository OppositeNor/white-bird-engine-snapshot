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
#include "core/parser/parser_toml.hh"
#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable_sd.hh"
#include "function/function_utils.hh"
#include "function/render_vk/rend_buffer_params_vk.hh"
#include "function/render_vk/rend_image_params_vk.hh"
#include "function/render_vk/rend_image_view_params_vk.hh"
#include "function/render_vk/rend_sampler_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "generated/enum_reflection.gen.hh"
#include "generated/serializables_sd.gen.hh"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

template <typename TParams>
TParams parse_resource_params(std::string_view p_json) {
    ParserJSON parser;
    parser.parse_from_buffer(p_json);
    TParams result{};
    SerializableSD<TParams>::deserialize(parser.get_data(), result);
    return result;
}

} // namespace

TEST(WBERendResourceParamsVKTest, EngineEnumsAreReflected) {
    EXPECT_EQ(EnumReflection<RendResPrimaryUsageVK>::string_to_enum("image_render_target"), RendResPrimaryUsageVK::IMAGE_RENDER_TARGET);
    EXPECT_EQ(EnumReflection<RendResPrimaryUsageVK>::enum_to_string(RendResPrimaryUsageVK::BUFFER_INDIRECT), "BUFFER_INDIRECT");
    EXPECT_EQ(EnumReflection<DeviceDomain>::string_to_enum("host_device"), DeviceDomain::HOST_DEVICE);
}

TEST(WBERendResourceParamsVKTest, VulkanMirrorsMatchVulkanValues) {
    EXPECT_EQ(to_vk_image_view_type(EnumReflection<ImageViewTypeVK>::string_to_enum("cube_array")), VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);
    EXPECT_EQ(to_vk_image_type(EnumReflection<ImageTypeVK>::string_to_enum("3d")), VK_IMAGE_TYPE_3D);
    EXPECT_EQ(to_vk_sampler_address_mode(EnumReflection<SamplerAddressModeVK>::string_to_enum("mirror_clamp_to_edge")),
        VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE);
    EXPECT_EQ(to_vk_compare_op(EnumReflection<CompareOpVK>::string_to_enum("greater_or_equal")), VK_COMPARE_OP_GREATER_OR_EQUAL);
    EXPECT_EQ(to_vk_border_color(EnumReflection<BorderColorVK>::string_to_enum("float_opaque_white")), VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    const ImageUsageFlagVK image_flags[] = {ImageUsageFlagVK::TRANSFER_SRC, ImageUsageFlagVK::SAMPLED, ImageUsageFlagVK::SAMPLED};
    EXPECT_EQ(to_vk_image_usage_flags(image_flags), VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    EXPECT_EQ(to_vk_buffer_usage_flags({}), VkBufferUsageFlags{0});
}

TEST(WBERendResourceParamsVKTest, BufferParamsConvert) {
    const RendBufferDescVK desc = parse_resource_params<RendBufferParamsVK>(
        R"({"usage": "buffer_indirect", "additional_usage": ["transfer_src", "transfer_dst"], "size": 256, "domain": "host_device", "per_frame": true})")
                                      .to_desc();
    EXPECT_EQ(desc.usage, RendResPrimaryUsageVK::BUFFER_INDIRECT);
    EXPECT_EQ(desc.additional_usage, VkBufferUsageFlags{VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT});
    EXPECT_EQ(desc.size, 256U);
    EXPECT_EQ(desc.domain, DeviceDomain::HOST_DEVICE);
    EXPECT_TRUE(desc.per_frame);
    EXPECT_THROW((void)parse_resource_params<RendBufferParamsVK>(R"({"usage": "buffer_storage"})"), std::runtime_error);
    EXPECT_THROW((void)parse_resource_params<RendBufferParamsVK>(R"({"size": 0})").to_desc(), std::runtime_error);
    EXPECT_THROW((void)parse_resource_params<RendBufferParamsVK>(R"({"size": 4, "sise": 4})"), std::runtime_error);
}

TEST(WBERendResourceParamsVKTest, ImageParamsFollowTheSwapChainUnlessFixed) {
    const RendGraphContextVK context{.swap_chain_extent = {.width = 800, .height = 600}};
    const RendImageParamsVK scaled = parse_resource_params<RendImageParamsVK>(
        R"({"usage": "image_render_target", "format": "r16g16b16a16_sfloat", "extent": {"swap_chain_scale": 0.5}, "additional_usage": ["transfer_src"], "mip_levels": 3})");
    const RendImageDescVK desc = scaled.to_desc(context);
    EXPECT_EQ(desc.usage, RendResPrimaryUsageVK::IMAGE_RENDER_TARGET);
    EXPECT_EQ(desc.format, VK_FORMAT_R16G16B16A16_SFLOAT);
    EXPECT_EQ(desc.extent.width, 400U);
    EXPECT_EQ(desc.extent.height, 300U);
    EXPECT_EQ(desc.extent.depth, 1U);
    EXPECT_EQ(desc.image_type, VK_IMAGE_TYPE_2D);
    EXPECT_EQ(desc.mip_levels, 3U);
    EXPECT_EQ(desc.additional_usage, VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT});

    const RendImageDescVK fixed = parse_resource_params<RendImageParamsVK>(
        R"({"format": "R32_UINT", "extent": {"width": 16, "height": 8}, "depth": 4, "image_type": "3d"})")
                                      .to_desc(context);
    EXPECT_EQ(fixed.extent.width, 16U);
    EXPECT_EQ(fixed.extent.depth, 4U);
    EXPECT_EQ(fixed.image_type, VK_IMAGE_TYPE_3D);
    EXPECT_THROW((void)parse_resource_params<RendImageParamsVK>(R"({"extent": {"width": 16, "height": 8}})"), std::runtime_error);
    EXPECT_THROW((void)parse_resource_params<RendImageParamsVK>(R"({"format": "r32_uint", "depth": 0})").to_desc(context), std::runtime_error);
}

TEST(WBERendResourceParamsVKTest, ImageViewParamsKeepUnsetFieldsDerived) {
    const RendImageViewDescVK derived = parse_resource_params<RendImageViewParamsVK>("{}").to_desc();
    EXPECT_FALSE(derived.view_type.has_value());
    EXPECT_FALSE(derived.format.has_value());
    EXPECT_FALSE(derived.aspect_mask.has_value());
    EXPECT_EQ(derived.mip_level_count, 1U);

    const RendImageViewDescVK explicit_view = parse_resource_params<RendImageViewParamsVK>(
        R"({"view_type": "2d_array", "format": "d32_sfloat", "aspect_mask": ["depth"], "base_mip_level": 2, "mip_level_count": 4294967295, "layer_count": 6})")
                                                  .to_desc();
    EXPECT_EQ(explicit_view.view_type, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    EXPECT_EQ(explicit_view.format, VK_FORMAT_D32_SFLOAT);
    EXPECT_EQ(explicit_view.aspect_mask, VkImageAspectFlags{VK_IMAGE_ASPECT_DEPTH_BIT});
    EXPECT_EQ(explicit_view.base_mip_level, 2U);
    EXPECT_EQ(explicit_view.mip_level_count, VK_REMAINING_MIP_LEVELS);
    EXPECT_EQ(explicit_view.layer_count, 6U);
    EXPECT_THROW((void)parse_resource_params<RendImageViewParamsVK>(R"({"aspect_mask": []})").to_desc(), std::runtime_error);
}

TEST(WBERendResourceParamsVKTest, SamplerParamsDefaultToTheDescriptorDefaults) {
    EXPECT_EQ(parse_resource_params<RendSamplerParamsVK>("{}").to_desc(), RendSamplerDescVK{});
    const RendSamplerDescVK shadow = parse_resource_params<RendSamplerParamsVK>(
        R"({"min_filter": "nearest", "address_mode_u": "clamp_to_border", "compare_op": "less", "max_anisotropy": 8, "border_color": "FLOAT_OPAQUE_WHITE"})")
                                         .to_desc();
    EXPECT_EQ(shadow.min_filter, VK_FILTER_NEAREST);
    EXPECT_EQ(shadow.mag_filter, VK_FILTER_LINEAR);
    EXPECT_EQ(shadow.address_mode_u, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    EXPECT_EQ(shadow.compare_op, VK_COMPARE_OP_LESS);
    EXPECT_EQ(shadow.max_anisotropy, 8.0F);
    EXPECT_EQ(shadow.border_color, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
    EXPECT_THROW((void)parse_resource_params<RendSamplerParamsVK>(R"({"min_filter": "cubic"})"), std::runtime_error);
}

TEST(WBERendResourceParamsVKTest, AcceptsTOMLAndScriptValue) {
    ParserTOML toml;
    toml.parse_from_buffer("format = \"rgba\"\n");
    RendImageParamsVK toml_result;
    EXPECT_THROW(SerializableSD<RendImageParamsVK>::deserialize(toml.get_data(), toml_result), std::runtime_error);
    toml.parse_from_buffer("format = \"b8g8r8a8_srgb\"\nadditional_usage = [\"storage\"]\n[extent]\nwidth = 2\nheight = 2\n");
    SerializableSD<RendImageParamsVK>::deserialize(toml.get_data(), toml_result);
    EXPECT_EQ(toml_result.to_desc(RendGraphContextVK{}).format, VK_FORMAT_B8G8R8A8_SRGB);

    DataValue script;
    script.set_entry("size", DataValue::make_integer(64));
    script.set_entry("usage", DataValue::make_string("buffer_uniform"));
    RendBufferParamsVK script_result;
    SerializableSD<RendBufferParamsVK>::deserialize(script, script_result);
    EXPECT_EQ(script_result.to_desc().usage, RendResPrimaryUsageVK::BUFFER_UNIFORM);
}

} // namespace WhiteBirdEngine

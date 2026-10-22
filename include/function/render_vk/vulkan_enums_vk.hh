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
#ifndef WBE_FILE_VULKAN_ENUMS_VK_HH
#define WBE_FILE_VULKAN_ENUMS_VK_HH

#include "core/reflection/reflection_defs.hh"
#include <span>
#include <type_traits>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

// Reflected mirrors of the Vulkan enums used by render graph recipes, which cannot annotate Vulkan's own headers. Each
// enumerator has the value of the Vulkan enumerator it names, so conversion is a cast; the aliases are the lowercase names
// recipes use. Flag enums name single bits, and a list of them combines into the Vulkan flags type.

/**
 * @enum ImageFormatVK
 * @brief Reflected subset of VkFormat. Add an enumerator to make another format available to recipes.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) ImageFormatVK : std::underlying_type_t<VkFormat> {
    UNDEFINED WBE_META(WBE_SERIALIZATION_ALIASES(undefined)) = VK_FORMAT_UNDEFINED,
    R8_UNORM WBE_META(WBE_SERIALIZATION_ALIASES(r8_unorm)) = VK_FORMAT_R8_UNORM,
    R8G8_UNORM WBE_META(WBE_SERIALIZATION_ALIASES(r8g8_unorm)) = VK_FORMAT_R8G8_UNORM,
    R8G8B8A8_UNORM WBE_META(WBE_SERIALIZATION_ALIASES(r8g8b8a8_unorm)) = VK_FORMAT_R8G8B8A8_UNORM,
    R8G8B8A8_SRGB WBE_META(WBE_SERIALIZATION_ALIASES(r8g8b8a8_srgb)) = VK_FORMAT_R8G8B8A8_SRGB,
    B8G8R8A8_UNORM WBE_META(WBE_SERIALIZATION_ALIASES(b8g8r8a8_unorm)) = VK_FORMAT_B8G8R8A8_UNORM,
    B8G8R8A8_SRGB WBE_META(WBE_SERIALIZATION_ALIASES(b8g8r8a8_srgb)) = VK_FORMAT_B8G8R8A8_SRGB,
    A2B10G10R10_UNORM_PACK32 WBE_META(WBE_SERIALIZATION_ALIASES(a2b10g10r10_unorm_pack32)) = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    B10G11R11_UFLOAT_PACK32 WBE_META(WBE_SERIALIZATION_ALIASES(b10g11r11_ufloat_pack32)) = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
    R16_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r16_sfloat)) = VK_FORMAT_R16_SFLOAT,
    R16G16_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r16g16_sfloat)) = VK_FORMAT_R16G16_SFLOAT,
    R16G16_SNORM WBE_META(WBE_SERIALIZATION_ALIASES(r16g16_snorm)) = VK_FORMAT_R16G16_SNORM,
    R16G16B16A16_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r16g16b16a16_sfloat)) = VK_FORMAT_R16G16B16A16_SFLOAT,
    R16_UINT WBE_META(WBE_SERIALIZATION_ALIASES(r16_uint)) = VK_FORMAT_R16_UINT,
    R16G16B16A16_UINT WBE_META(WBE_SERIALIZATION_ALIASES(r16g16b16a16_uint)) = VK_FORMAT_R16G16B16A16_UINT,
    R32_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r32_sfloat)) = VK_FORMAT_R32_SFLOAT,
    R32G32_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r32g32_sfloat)) = VK_FORMAT_R32G32_SFLOAT,
    R32G32B32_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r32g32b32_sfloat)) = VK_FORMAT_R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(r32g32b32a32_sfloat)) = VK_FORMAT_R32G32B32A32_SFLOAT,
    R32_UINT WBE_META(WBE_SERIALIZATION_ALIASES(r32_uint)) = VK_FORMAT_R32_UINT,
    R32G32_UINT WBE_META(WBE_SERIALIZATION_ALIASES(r32g32_uint)) = VK_FORMAT_R32G32_UINT,
    R32G32B32_UINT WBE_META(WBE_SERIALIZATION_ALIASES(r32g32b32_uint)) = VK_FORMAT_R32G32B32_UINT,
    R32G32B32A32_UINT WBE_META(WBE_SERIALIZATION_ALIASES(r32g32b32a32_uint)) = VK_FORMAT_R32G32B32A32_UINT,
    R32_SINT WBE_META(WBE_SERIALIZATION_ALIASES(r32_sint)) = VK_FORMAT_R32_SINT,
    D16_UNORM WBE_META(WBE_SERIALIZATION_ALIASES(d16_unorm)) = VK_FORMAT_D16_UNORM,
    D32_SFLOAT WBE_META(WBE_SERIALIZATION_ALIASES(d32_sfloat)) = VK_FORMAT_D32_SFLOAT,
    D24_UNORM_S8_UINT WBE_META(WBE_SERIALIZATION_ALIASES(d24_unorm_s8_uint)) = VK_FORMAT_D24_UNORM_S8_UINT,
    D32_SFLOAT_S8_UINT WBE_META(WBE_SERIALIZATION_ALIASES(d32_sfloat_s8_uint)) = VK_FORMAT_D32_SFLOAT_S8_UINT,
};

/**
 * @enum AttachmentLoadOpVK
 * @brief Reflected mirror of VkAttachmentLoadOp.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) AttachmentLoadOpVK : std::underlying_type_t<VkAttachmentLoadOp> {
    LOAD WBE_META(WBE_SERIALIZATION_ALIASES(load)) = VK_ATTACHMENT_LOAD_OP_LOAD,
    CLEAR WBE_META(WBE_SERIALIZATION_ALIASES(clear)) = VK_ATTACHMENT_LOAD_OP_CLEAR,
    DONT_CARE WBE_META(WBE_SERIALIZATION_ALIASES(dont_care)) = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
};

/**
 * @enum ImageTypeVK
 * @brief Reflected mirror of VkImageType.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) ImageTypeVK : std::underlying_type_t<VkImageType> {
    TYPE_1D WBE_META(WBE_SERIALIZATION_ALIASES(1d)) = VK_IMAGE_TYPE_1D,
    TYPE_2D WBE_META(WBE_SERIALIZATION_ALIASES(2d)) = VK_IMAGE_TYPE_2D,
    TYPE_3D WBE_META(WBE_SERIALIZATION_ALIASES(3d)) = VK_IMAGE_TYPE_3D,
};

/**
 * @enum ImageViewTypeVK
 * @brief Reflected mirror of VkImageViewType.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) ImageViewTypeVK : std::underlying_type_t<VkImageViewType> {
    TYPE_1D WBE_META(WBE_SERIALIZATION_ALIASES(1d)) = VK_IMAGE_VIEW_TYPE_1D,
    TYPE_2D WBE_META(WBE_SERIALIZATION_ALIASES(2d)) = VK_IMAGE_VIEW_TYPE_2D,
    TYPE_3D WBE_META(WBE_SERIALIZATION_ALIASES(3d)) = VK_IMAGE_VIEW_TYPE_3D,
    CUBE WBE_META(WBE_SERIALIZATION_ALIASES(cube)) = VK_IMAGE_VIEW_TYPE_CUBE,
    TYPE_1D_ARRAY WBE_META(WBE_SERIALIZATION_ALIASES(1d_array)) = VK_IMAGE_VIEW_TYPE_1D_ARRAY,
    TYPE_2D_ARRAY WBE_META(WBE_SERIALIZATION_ALIASES(2d_array)) = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
    CUBE_ARRAY WBE_META(WBE_SERIALIZATION_ALIASES(cube_array)) = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
};

/**
 * @enum FilterVK
 * @brief Reflected mirror of VkFilter.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) FilterVK : std::underlying_type_t<VkFilter> {
    NEAREST WBE_META(WBE_SERIALIZATION_ALIASES(nearest)) = VK_FILTER_NEAREST,
    LINEAR WBE_META(WBE_SERIALIZATION_ALIASES(linear)) = VK_FILTER_LINEAR,
};

/**
 * @enum SamplerMipmapModeVK
 * @brief Reflected mirror of VkSamplerMipmapMode.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) SamplerMipmapModeVK : std::underlying_type_t<VkSamplerMipmapMode> {
    NEAREST WBE_META(WBE_SERIALIZATION_ALIASES(nearest)) = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    LINEAR WBE_META(WBE_SERIALIZATION_ALIASES(linear)) = VK_SAMPLER_MIPMAP_MODE_LINEAR,
};

/**
 * @enum SamplerAddressModeVK
 * @brief Reflected mirror of VkSamplerAddressMode.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) SamplerAddressModeVK : std::underlying_type_t<VkSamplerAddressMode> {
    REPEAT WBE_META(WBE_SERIALIZATION_ALIASES(repeat)) = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    MIRRORED_REPEAT WBE_META(WBE_SERIALIZATION_ALIASES(mirrored_repeat)) = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    CLAMP_TO_EDGE WBE_META(WBE_SERIALIZATION_ALIASES(clamp_to_edge)) = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    CLAMP_TO_BORDER WBE_META(WBE_SERIALIZATION_ALIASES(clamp_to_border)) = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    MIRROR_CLAMP_TO_EDGE WBE_META(WBE_SERIALIZATION_ALIASES(mirror_clamp_to_edge)) = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
};

/**
 * @enum CompareOpVK
 * @brief Reflected mirror of VkCompareOp.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) CompareOpVK : std::underlying_type_t<VkCompareOp> {
    NEVER WBE_META(WBE_SERIALIZATION_ALIASES(never)) = VK_COMPARE_OP_NEVER,
    LESS WBE_META(WBE_SERIALIZATION_ALIASES(less)) = VK_COMPARE_OP_LESS,
    EQUAL WBE_META(WBE_SERIALIZATION_ALIASES(equal)) = VK_COMPARE_OP_EQUAL,
    LESS_OR_EQUAL WBE_META(WBE_SERIALIZATION_ALIASES(less_or_equal)) = VK_COMPARE_OP_LESS_OR_EQUAL,
    GREATER WBE_META(WBE_SERIALIZATION_ALIASES(greater)) = VK_COMPARE_OP_GREATER,
    NOT_EQUAL WBE_META(WBE_SERIALIZATION_ALIASES(not_equal)) = VK_COMPARE_OP_NOT_EQUAL,
    GREATER_OR_EQUAL WBE_META(WBE_SERIALIZATION_ALIASES(greater_or_equal)) = VK_COMPARE_OP_GREATER_OR_EQUAL,
    ALWAYS WBE_META(WBE_SERIALIZATION_ALIASES(always)) = VK_COMPARE_OP_ALWAYS,
};

/**
 * @enum BorderColorVK
 * @brief Reflected mirror of VkBorderColor.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) BorderColorVK : std::underlying_type_t<VkBorderColor> {
    FLOAT_TRANSPARENT_BLACK WBE_META(WBE_SERIALIZATION_ALIASES(float_transparent_black)) = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    INT_TRANSPARENT_BLACK WBE_META(WBE_SERIALIZATION_ALIASES(int_transparent_black)) = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
    FLOAT_OPAQUE_BLACK WBE_META(WBE_SERIALIZATION_ALIASES(float_opaque_black)) = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    INT_OPAQUE_BLACK WBE_META(WBE_SERIALIZATION_ALIASES(int_opaque_black)) = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    FLOAT_OPAQUE_WHITE WBE_META(WBE_SERIALIZATION_ALIASES(float_opaque_white)) = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    INT_OPAQUE_WHITE WBE_META(WBE_SERIALIZATION_ALIASES(int_opaque_white)) = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
};

/**
 * @enum BufferUsageFlagVK
 * @brief Reflected subset of VkBufferUsageFlagBits; a list of flags combines into VkBufferUsageFlags.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) BufferUsageFlagVK : VkFlags {
    TRANSFER_SRC WBE_META(WBE_SERIALIZATION_ALIASES(transfer_src)) = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    TRANSFER_DST WBE_META(WBE_SERIALIZATION_ALIASES(transfer_dst)) = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    UNIFORM_TEXEL_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(uniform_texel_buffer)) = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
    STORAGE_TEXEL_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(storage_texel_buffer)) = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
    UNIFORM_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(uniform_buffer)) = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    STORAGE_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(storage_buffer)) = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    INDEX_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(index_buffer)) = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    VERTEX_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(vertex_buffer)) = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    INDIRECT_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(indirect_buffer)) = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
    SHADER_DEVICE_ADDRESS WBE_META(WBE_SERIALIZATION_ALIASES(shader_device_address)) = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY WBE_META(WBE_SERIALIZATION_ALIASES(
        acceleration_structure_build_input_read_only)) = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
    ACCELERATION_STRUCTURE_STORAGE WBE_META(
        WBE_SERIALIZATION_ALIASES(acceleration_structure_storage)) = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
};

/**
 * @enum ImageUsageFlagVK
 * @brief Reflected mirror of VkImageUsageFlagBits; a list of flags combines into VkImageUsageFlags.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) ImageUsageFlagVK : VkFlags {
    TRANSFER_SRC WBE_META(WBE_SERIALIZATION_ALIASES(transfer_src)) = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    TRANSFER_DST WBE_META(WBE_SERIALIZATION_ALIASES(transfer_dst)) = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    SAMPLED WBE_META(WBE_SERIALIZATION_ALIASES(sampled)) = VK_IMAGE_USAGE_SAMPLED_BIT,
    STORAGE WBE_META(WBE_SERIALIZATION_ALIASES(storage)) = VK_IMAGE_USAGE_STORAGE_BIT,
    COLOR_ATTACHMENT WBE_META(WBE_SERIALIZATION_ALIASES(color_attachment)) = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    DEPTH_STENCIL_ATTACHMENT WBE_META(WBE_SERIALIZATION_ALIASES(depth_stencil_attachment)) = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    TRANSIENT_ATTACHMENT WBE_META(WBE_SERIALIZATION_ALIASES(transient_attachment)) = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
    INPUT_ATTACHMENT WBE_META(WBE_SERIALIZATION_ALIASES(input_attachment)) = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
};

/**
 * @enum ImageAspectFlagVK
 * @brief Reflected subset of VkImageAspectFlagBits; a list of flags combines into VkImageAspectFlags.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) ImageAspectFlagVK : VkFlags {
    COLOR WBE_META(WBE_SERIALIZATION_ALIASES(color)) = VK_IMAGE_ASPECT_COLOR_BIT,
    DEPTH WBE_META(WBE_SERIALIZATION_ALIASES(depth)) = VK_IMAGE_ASPECT_DEPTH_BIT,
    STENCIL WBE_META(WBE_SERIALIZATION_ALIASES(stencil)) = VK_IMAGE_ASPECT_STENCIL_BIT,
};

/**
 * @brief Convert to the VkFormat with the same value.
 */
constexpr VkFormat to_vk_format(ImageFormatVK p_value) {
    return static_cast<VkFormat>(p_value);
}

/**
 * @brief Convert to the VkAttachmentLoadOp with the same value.
 */
constexpr VkAttachmentLoadOp to_vk_attachment_load_op(AttachmentLoadOpVK p_value) {
    return static_cast<VkAttachmentLoadOp>(p_value);
}

/**
 * @brief Convert to the VkImageType with the same value.
 */
constexpr VkImageType to_vk_image_type(ImageTypeVK p_value) {
    return static_cast<VkImageType>(p_value);
}

/**
 * @brief Convert to the VkImageViewType with the same value.
 */
constexpr VkImageViewType to_vk_image_view_type(ImageViewTypeVK p_value) {
    return static_cast<VkImageViewType>(p_value);
}

/**
 * @brief Convert to the VkFilter with the same value.
 */
constexpr VkFilter to_vk_filter(FilterVK p_value) {
    return static_cast<VkFilter>(p_value);
}

/**
 * @brief Convert to the VkSamplerMipmapMode with the same value.
 */
constexpr VkSamplerMipmapMode to_vk_sampler_mipmap_mode(SamplerMipmapModeVK p_value) {
    return static_cast<VkSamplerMipmapMode>(p_value);
}

/**
 * @brief Convert to the VkSamplerAddressMode with the same value.
 */
constexpr VkSamplerAddressMode to_vk_sampler_address_mode(SamplerAddressModeVK p_value) {
    return static_cast<VkSamplerAddressMode>(p_value);
}

/**
 * @brief Convert to the VkCompareOp with the same value.
 */
constexpr VkCompareOp to_vk_compare_op(CompareOpVK p_value) {
    return static_cast<VkCompareOp>(p_value);
}

/**
 * @brief Convert to the VkBorderColor with the same value.
 */
constexpr VkBorderColor to_vk_border_color(BorderColorVK p_value) {
    return static_cast<VkBorderColor>(p_value);
}

/**
 * @brief Combine BufferUsageFlagVK bits into VkBufferUsageFlags.
 */
inline VkBufferUsageFlags to_vk_buffer_usage_flags(std::span<const BufferUsageFlagVK> p_flags) {
    VkBufferUsageFlags result = 0;
    for (BufferUsageFlagVK flag : p_flags) {
        result |= static_cast<VkBufferUsageFlags>(flag);
    }
    return result;
}

/**
 * @brief Combine ImageUsageFlagVK bits into VkImageUsageFlags.
 */
inline VkImageUsageFlags to_vk_image_usage_flags(std::span<const ImageUsageFlagVK> p_flags) {
    VkImageUsageFlags result = 0;
    for (ImageUsageFlagVK flag : p_flags) {
        result |= static_cast<VkImageUsageFlags>(flag);
    }
    return result;
}

/**
 * @brief Combine ImageAspectFlagVK bits into VkImageAspectFlags.
 */
inline VkImageAspectFlags to_vk_image_aspect_flags(std::span<const ImageAspectFlagVK> p_flags) {
    VkImageAspectFlags result = 0;
    for (ImageAspectFlagVK flag : p_flags) {
        result |= static_cast<VkImageAspectFlags>(flag);
    }
    return result;
}

} // namespace WhiteBirdEngine

#endif

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
#include "core/core_utils.hh"
#include "function/function_utils.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include <stdexcept>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

TEST(WBEROImageVKBuilderTest, DefaultViewRejectsUnavailableImageFormat) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ROImageVK image(&render_data, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);

    EXPECT_THROW(image.get_image_view_ref(), std::runtime_error);
}

TEST(WBEROImageVKBuilderTest, ExplicitViewRejectsInvalidSubresourceRanges) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ROImageVK image(&render_data, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);
    VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 1, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

    EXPECT_THROW(image.get_image_view_ref(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, range, {}), std::runtime_error);
    range.baseMipLevel = 0;
    range.levelCount = 2;
    EXPECT_THROW(image.get_image_view_ref(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, range, {}), std::runtime_error);
    range.levelCount = 0;
    EXPECT_THROW(image.get_image_view_ref(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, range, {}), std::runtime_error);
}

TEST(WBEROImageVKBuilderTest, ExplicitViewRequiresSharedImageOwnership) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ROImageVK image(&render_data, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED);
    const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS};

    EXPECT_THROW(image.get_image_view_ref(VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, range, {}), std::runtime_error);
}

TEST(WBEROImageVKBuilderTest, SetProperties2DConfiguresRequiredProperties) {
    constexpr VkExtent2D IMAGE_EXTENT{.width = 640, .height = 480};
    constexpr VkImageUsageFlags IMAGE_USAGE = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ROImageVK::Builder builder;

    ROImageVK::Builder& result = builder.set_properties_2d(VK_FORMAT_R8G8B8A8_UNORM, IMAGE_EXTENT, IMAGE_USAGE);

    EXPECT_EQ(&result, &builder);
    EXPECT_EQ(WBE_REQUIRED(builder.image_type), VK_IMAGE_TYPE_2D);
    EXPECT_EQ(WBE_REQUIRED(builder.format), VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(WBE_REQUIRED(builder.extent).width, IMAGE_EXTENT.width);
    EXPECT_EQ(WBE_REQUIRED(builder.extent).height, IMAGE_EXTENT.height);
    EXPECT_EQ(WBE_REQUIRED(builder.extent).depth, 1U);
    EXPECT_EQ(WBE_REQUIRED(builder.usage), IMAGE_USAGE);
}

} // namespace WhiteBirdEngine

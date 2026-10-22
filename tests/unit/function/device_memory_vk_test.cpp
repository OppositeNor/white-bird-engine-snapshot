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
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/function_utils.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/device_memory_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

TEST(WBEDeviceMemoryVKTest, BuilderRecordsExplicitRequirementsAndDomain) {
    DeviceMemoryVK::Builder builder;
    const VkMemoryRequirements requirements{.size = 256, .alignment = 64, .memoryTypeBits = 0x3};

    DeviceMemoryVK::Builder& result = builder.set_requirements(requirements).set_domain(DeviceDomain::HOST_DEVICE);

    EXPECT_EQ(&result, &builder);
    ASSERT_TRUE(builder.requirements.has_value());
    EXPECT_EQ(builder.requirements->size, 256U);
    EXPECT_EQ(builder.requirements->alignment, 64U);
    EXPECT_EQ(WBE_REQUIRED(builder.domain), DeviceDomain::HOST_DEVICE);
    EXPECT_EQ(builder.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(builder.image, VK_NULL_HANDLE);
}

TEST(WBEDeviceMemoryVKTest, RequiresExactlyOneRequirementSource) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    EXPECT_THROW(DeviceMemoryVK(&render_data, DeviceMemoryVK::Builder().set_domain(DeviceDomain::DEVICE)), std::runtime_error);

    DeviceMemoryVK::Builder two_sources;
    two_sources.set_requirements(VkMemoryRequirements{.size = 16, .alignment = 16, .memoryTypeBits = 1})
        .set_buffer_requirements(reinterpret_cast<VkBuffer>(uintptr_t{1}))
        .set_domain(DeviceDomain::DEVICE);
    EXPECT_THROW(DeviceMemoryVK(&render_data, two_sources), std::runtime_error);
}

TEST(WBEDeviceMemoryVKTest, AliasBuildersRequireMemory) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    ROBufferVK::AliasBuilder buffer_builder;
    buffer_builder.set_buffer(ROBufferVK::Builder().set_properties(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    EXPECT_EQ(buffer_builder.memory, MEM_NULL);
    EXPECT_THROW(ROBufferVK(&render_data, buffer_builder), std::runtime_error);

    ROImageVK::AliasBuilder image_builder;
    image_builder.set_image(ROImageVK::Builder().set_properties_2d(
        VK_FORMAT_R8G8B8A8_UNORM, VkExtent2D{.width = 4, .height = 4}, VK_IMAGE_USAGE_STORAGE_BIT));
    EXPECT_THROW(ROImageVK(&render_data, image_builder), std::runtime_error);
}

} // namespace WhiteBirdEngine

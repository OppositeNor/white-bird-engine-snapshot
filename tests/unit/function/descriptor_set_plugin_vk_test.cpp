/* Copyright 2026 OppositeNor

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
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/descriptor_set_plugin_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

TEST(WBEDescriptorSetPluginVKTest, EnablesOnlyWhenRequested) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    DescriptorSetPluginVK plugin(&render_data);

    RendererCapabilities capabilities{};
    EXPECT_FALSE(plugin.try_enable(capabilities, VK_NULL_HANDLE, nullptr, 0));
    EXPECT_FALSE(plugin.is_enabled());

    capabilities.descriptor_set = true;
    EXPECT_TRUE(plugin.try_enable(capabilities, VK_NULL_HANDLE, nullptr, 0));
    EXPECT_TRUE(plugin.is_enabled());
    EXPECT_TRUE(capabilities.descriptor_set);
    EXPECT_TRUE(plugin.get_required_instance_extensions().empty());
    EXPECT_TRUE(plugin.get_required_device_extensions().empty());
}

TEST(WBEDescriptorSetPluginVKTest, RegistersSingleton) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    EXPECT_EQ(DescriptorSetPluginVK::get_singleton(), nullptr);
    {
        DescriptorSetPluginVK plugin(&render_data);
        EXPECT_EQ(DescriptorSetPluginVK::get_singleton(), &plugin);
    }
    EXPECT_EQ(DescriptorSetPluginVK::get_singleton(), nullptr);
}

TEST(WBEDescriptorSetPluginVKTest, ThrowsBeforeVulkanObjectsExist) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    DescriptorSetPluginVK plugin(&render_data);

    EXPECT_THROW(static_cast<void>(plugin.get_instance()), std::runtime_error);
    EXPECT_THROW(static_cast<void>(plugin.get_descriptor_pool()), std::runtime_error);
    EXPECT_THROW(static_cast<void>(plugin.allocate_descriptor_set(VK_NULL_HANDLE)), std::runtime_error);
    EXPECT_THROW(plugin.free_descriptor_set(VK_NULL_HANDLE), std::runtime_error);

    auto* instance = reinterpret_cast<VkInstance>(uintptr_t{1}); // NOLINT
    plugin.on_after_instance_created(instance);
    EXPECT_EQ(plugin.get_instance(), instance);
}

} // namespace WhiteBirdEngine

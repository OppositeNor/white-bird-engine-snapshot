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
#include "function/render/render_defs.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

struct RecordedObjectName {
    int call_count = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkObjectType type = VK_OBJECT_TYPE_UNKNOWN;
    uint64_t handle = 0;
    std::string name;
};

RecordedObjectName& recorded_object_name() {
    static RecordedObjectName recorded;
    return recorded;
}

VKAPI_ATTR VkResult VKAPI_CALL record_object_name(VkDevice p_device, const VkDebugUtilsObjectNameInfoEXT* p_name_info) {
    RecordedObjectName& recorded = recorded_object_name();
    ++recorded.call_count;
    recorded.device = p_device;
    recorded.type = p_name_info->objectType;
    recorded.handle = p_name_info->objectHandle;
    recorded.name = p_name_info->pObjectName;
    return VK_SUCCESS;
}

class WBEDebugUtilsVKTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        recorded_object_name() = RecordedObjectName{};
        render_data.logic_device = reinterpret_cast<VkDevice>(uintptr_t{0x10});
        render_data.set_debug_utils_object_name = &record_object_name;
    }

    RenderAllocator allocator{WBE_KI_B(16)};
    RenderContextVK render_data{&allocator};
};

} // namespace

TEST_F(WBEDebugUtilsVKTest, NamesObjectsThroughTheExtensionCommand) {
    const auto buffer = reinterpret_cast<VkBuffer>(uintptr_t{0x1234});
    set_debug_object_name_vk(&render_data, VK_OBJECT_TYPE_BUFFER, buffer, std::string_view("sponza.nodes_and_more").substr(0, 11));
    const RecordedObjectName& recorded = recorded_object_name();
    ASSERT_EQ(recorded.call_count, 1);
    EXPECT_EQ(recorded.device, render_data.logic_device);
    EXPECT_EQ(recorded.type, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(recorded.handle, uint64_t{0x1234});
    // A substring view is passed NUL-terminated.
    EXPECT_EQ(recorded.name, "sponza.node");
}

TEST_F(WBEDebugUtilsVKTest, DoesNothingWithoutTheExtensionOrAHandleOrAName) {
    set_debug_object_name_vk(&render_data, VK_OBJECT_TYPE_BUFFER, VK_NULL_HANDLE, "named");
    set_debug_object_name_vk(&render_data, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<VkBuffer>(uintptr_t{1}), "");
    set_debug_object_name_vk(nullptr, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<VkBuffer>(uintptr_t{1}), "named");
    render_data.set_debug_utils_object_name = nullptr;
    set_debug_object_name_vk(&render_data, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<VkBuffer>(uintptr_t{1}), "named");
    EXPECT_EQ(recorded_object_name().call_count, 0);
}

} // namespace WhiteBirdEngine

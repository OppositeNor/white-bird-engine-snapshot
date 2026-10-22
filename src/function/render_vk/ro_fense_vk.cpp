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
#include "function/render_vk/ro_fense_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

ROFenseVK::ROFenseVK(const RenderContextVK* p_render_data, bool p_signaled) : render_data(p_render_data) {
    WBE_DEBUG_ASSERT(p_render_data != nullptr);
    VkFenceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (p_signaled) {
        create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    VkResult result = vkCreateFence(render_data->logic_device, &create_info, render_data->allocation_callbacks, &fense);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create fense: {}.", string_VkResult(result)));
    }
}

ROFenseVK::~ROFenseVK() {
    render_data->register_delayed_call(
        [render_data = render_data, fense = fense]() {
            vkDestroyFence(render_data->logic_device, fense, render_data->allocation_callbacks);
        },
        render_data->frames_in_flight);
}

void ROFenseVK::wait() const {
    VkResult result = vkWaitForFences(render_data->logic_device, 1, &fense, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to wait for fense: {}.", string_VkResult(result)));
    }
}

void ROFenseVK::reset() {
    VkResult result = vkResetFences(render_data->logic_device, 1, &fense);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to reset fense: {}.", string_VkResult(result)));
    }
}

} // namespace WhiteBirdEngine

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
#ifndef WBE_FILE_KTX_VULKAN_DEVICE_INFO_VK_HH
#define WBE_FILE_KTX_VULKAN_DEVICE_INFO_VK_HH

#include "function/render_vk/ktx_vulkan_adapter.hh"
#include <ktx.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class KTXVulkanDeviceInfoVK
 * @brief Owns the reusable libktx Vulkan upload context for one renderer.
 *
 * Calls must follow the renderer's Vulkan threading contract: before the render thread starts or from the render thread itself.
 */
class KTXVulkanDeviceInfoVK final {
public:
    /**
     * @brief Construct a libktx Vulkan upload context.
     * @throws std::runtime_error When libktx cannot initialize the context.
     */
    KTXVulkanDeviceInfoVK(VkInstance p_instance,
        VkPhysicalDevice p_physical_device,
        VkDevice p_device,
        VkQueue p_queue,
        VkCommandPool p_command_pool,
        const VkAllocationCallbacks* p_allocation_callbacks);
    ~KTXVulkanDeviceInfoVK();
    KTXVulkanDeviceInfoVK(const KTXVulkanDeviceInfoVK&) = delete;
    KTXVulkanDeviceInfoVK(KTXVulkanDeviceInfoVK&&) noexcept = delete;
    KTXVulkanDeviceInfoVK& operator=(const KTXVulkanDeviceInfoVK&) = delete;
    KTXVulkanDeviceInfoVK& operator=(KTXVulkanDeviceInfoVK&&) noexcept = delete;

    /**
     * @brief Upload a directly uploadable KTX2 texture and transfer ownership of the returned Vulkan allocation to the caller.
     * @throws std::runtime_error When transcoding is still required or the Vulkan upload fails.
     */
    ktxVulkanTexture upload(ktxTexture2* p_texture);

private:
    ktxVulkanDeviceInfo device_info{};
};

} // namespace WhiteBirdEngine

#endif

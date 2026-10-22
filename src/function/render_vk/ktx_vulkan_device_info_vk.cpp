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
#include "function/render_vk/ktx_vulkan_device_info_vk.hh"
#include "function/render_vk/ktx_vulkan_adapter.hh"
#include "ktx.h"

#include <format>
#include <stdexcept>
#include <string_view>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

static void throw_texture_transition_error(std::string_view p_operation, VkResult p_result) {
    throw std::runtime_error(std::format(
        "Failed to transition uploaded KTX2 texture to the engine image layout during {}: {}.", p_operation, string_VkResult(p_result)));
}

static void transition_texture_to_general(ktxVulkanTexture& p_texture, const ktxVulkanDeviceInfo& p_device_info) {
    VkResult result = vkResetCommandBuffer(p_device_info.cmdBuffer, 0);
    if (result != VK_SUCCESS) {
        throw_texture_transition_error("command-buffer reset", result);
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(p_device_info.cmdBuffer, &begin_info);
    if (result != VK_SUCCESS) {
        throw_texture_transition_error("command-buffer begin", result);
    }

    // libktx cannot target GENERAL, so it first finishes in SHADER_READ_ONLY_OPTIMAL. Changing only ktxVulkanTexture::imageLayout
    // would not change the GPU image layout; this barrier establishes the engine's unified GENERAL layout convention.
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = p_texture.image;
    barrier.subresourceRange = VkImageSubresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = p_texture.levelCount,
        .baseArrayLayer = 0,
        .layerCount = p_texture.layerCount};
    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(p_device_info.cmdBuffer, &dependency_info);

    result = vkEndCommandBuffer(p_device_info.cmdBuffer);
    if (result != VK_SUCCESS) {
        throw_texture_transition_error("command-buffer end", result);
    }
    VkCommandBufferSubmitInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_buffer_info.commandBuffer = p_device_info.cmdBuffer;
    VkSubmitInfo2 submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &command_buffer_info;
    result = vkQueueSubmit2(p_device_info.queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        throw_texture_transition_error("queue submission", result);
    }
    result = vkQueueWaitIdle(p_device_info.queue);
    if (result != VK_SUCCESS) {
        throw_texture_transition_error("queue wait", result);
    }
    p_texture.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
}

KTXVulkanDeviceInfoVK::KTXVulkanDeviceInfoVK(VkInstance p_instance,
    VkPhysicalDevice p_physical_device,
    VkDevice p_device,
    VkQueue p_queue,
    VkCommandPool p_command_pool,
    const VkAllocationCallbacks* p_allocation_callbacks) {
    ktxVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    KTX_error_code result = ktxVulkanDeviceInfo_ConstructEx(
        &device_info, p_instance, p_physical_device, p_device, p_queue, p_command_pool, p_allocation_callbacks, &functions);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(std::format("Failed to construct libktx Vulkan device info: {}.", ktxErrorString(result)));
    }
}

KTXVulkanDeviceInfoVK::~KTXVulkanDeviceInfoVK() {
    ktxVulkanDeviceInfo_Destruct(&device_info);
}

ktxVulkanTexture KTXVulkanDeviceInfoVK::upload(ktxTexture2* p_texture) {
    if (p_texture == nullptr) {
        throw std::runtime_error("Failed to upload KTX2 texture: texture is null.");
    }
    if (ktxTexture2_NeedsTranscoding(p_texture)) {
        throw std::runtime_error("Failed to upload KTX2 texture: runtime transcoding is not supported.");
    }
    if (ktxTexture2_GetVkFormat(p_texture) == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("Failed to upload KTX2 texture: Vulkan format is undefined.");
    }

    ktxVulkanTexture texture{};
    // We're using VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL here because ktxTexture2_VkUploadEx does not support
    // VK_IMAGE_LAYOUT_GENERAL as a final layout.
    KTX_error_code result = ktxTexture2_VkUploadEx(
        p_texture, &device_info, &texture, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (result != KTX_SUCCESS) {
        throw std::runtime_error(std::format("Failed to upload KTX2 texture to Vulkan: {}.", ktxErrorString(result)));
    }
    transition_texture_to_general(texture, device_info);
    return texture;
}

} // namespace WhiteBirdEngine

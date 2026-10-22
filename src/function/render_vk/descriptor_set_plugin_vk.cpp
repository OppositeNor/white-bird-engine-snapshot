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
#include "function/render_vk/descriptor_set_plugin_vk.hh"
#include "function/render/i_render_task.hh"
#include "function/render/i_renderer.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "utils/defs.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

constexpr std::array<VkDescriptorType, 7> POOL_DESCRIPTOR_TYPES{
    VK_DESCRIPTOR_TYPE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
};

} // namespace

DescriptorSetPluginVK::DescriptorSetPluginVK(const RenderContextVK* p_render_data) : render_data(p_render_data) {
    singleton = this;
}

DescriptorSetPluginVK::~DescriptorSetPluginVK() {
    // The pool is released by on_renderer_terminating(); this only covers renderers that failed before termination.
    destroy_descriptor_pool();
    singleton = nullptr;
}

VkInstance DescriptorSetPluginVK::get_instance() const {
    if (vk_instance == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorSetPluginVK: the Vulkan instance has not been created.");
    }
    return vk_instance;
}

VkDescriptorPool DescriptorSetPluginVK::get_descriptor_pool() const {
    if (descriptor_pool == VK_NULL_HANDLE) {
        throw std::runtime_error("DescriptorSetPluginVK: the descriptor pool is unavailable.");
    }
    return descriptor_pool;
}

VkDescriptorSet DescriptorSetPluginVK::allocate_descriptor_set(VkDescriptorSetLayout p_layout) {
    VkDescriptorSetAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool = get_descriptor_pool();
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &p_layout;
    VkDescriptorSet result = VK_NULL_HANDLE;
    VkResult allocate_result = vkAllocateDescriptorSets(logical_device, &allocate_info, &result);
    if (allocate_result != VK_SUCCESS) {
        throw std::runtime_error(
            std::format("DescriptorSetPluginVK: failed to allocate a descriptor set: {}.", string_VkResult(allocate_result)));
    }
    return result;
}

void DescriptorSetPluginVK::free_descriptor_set(VkDescriptorSet p_descriptor_set) {
    VkResult free_result = vkFreeDescriptorSets(logical_device, get_descriptor_pool(), 1, &p_descriptor_set);
    if (free_result != VK_SUCCESS) {
        throw std::runtime_error(
            std::format("DescriptorSetPluginVK: failed to free a descriptor set: {}.", string_VkResult(free_result)));
    }
}

bool DescriptorSetPluginVK::try_enable(
    RendererCapabilities& p_capabilities, VkPhysicalDevice /*p_physical_device*/, const char* const* /*p_enabled_layers*/, size_t /*p_enabled_layer_count*/) {
    enabled = p_capabilities.descriptor_set;
    return enabled;
}

std::span<const char* const> DescriptorSetPluginVK::get_required_instance_extensions() const {
    return {};
}

std::span<const char* const> DescriptorSetPluginVK::get_required_device_extensions() const {
    // Descriptor sets are core Vulkan.
    return {};
}

void DescriptorSetPluginVK::on_before_instance_creation(VkInstanceCreateInfo* /*p_create_info*/) {
}

void DescriptorSetPluginVK::on_after_instance_created(VkInstance p_instance) {
    vk_instance = p_instance;
}

void DescriptorSetPluginVK::on_before_device_creation(
    VkPhysicalDevice /*p_physical_device*/, VkDeviceCreateInfo* /*p_create_info*/, VkPhysicalDeviceFeatures* /*p_device_features*/) {
}

void DescriptorSetPluginVK::on_after_device_created(
    RenderContextVK* /*p_render_data*/, VkPhysicalDevice /*p_physical_device*/, VkDevice p_logical_device) {
    WBE_DEBUG_ASSERT(enabled);
    WBE_DEBUG_ASSERT(descriptor_pool == VK_NULL_HANDLE);
    logical_device = p_logical_device;
    std::array<VkDescriptorPoolSize, POOL_DESCRIPTOR_TYPES.size()> pool_sizes{};
    for (size_t i = 0; i < POOL_DESCRIPTOR_TYPES.size(); ++i) {
        pool_sizes[i] = VkDescriptorPoolSize{.type = POOL_DESCRIPTOR_TYPES[i], .descriptorCount = DESCRIPTORS_PER_TYPE};
    }
    VkDescriptorPoolCreateInfo pool_create_info{};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_create_info.maxSets = MAX_DESCRIPTOR_SETS;
    pool_create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_create_info.pPoolSizes = pool_sizes.data();
    VkResult create_result =
        vkCreateDescriptorPool(logical_device, &pool_create_info, render_data->allocation_callbacks, &descriptor_pool);
    if (create_result != VK_SUCCESS) {
        descriptor_pool = VK_NULL_HANDLE;
        throw std::runtime_error(
            std::format("DescriptorSetPluginVK: failed to create the descriptor pool: {}.", string_VkResult(create_result)));
    }
}

void DescriptorSetPluginVK::update_render_context(RenderContextVK* /*p_render_data*/, VkPhysicalDevice /*p_physical_device*/) {
}

void DescriptorSetPluginVK::configure_descriptor_heaps(
    const RenderContextVK* /*p_render_data*/, std::span<DescriptorHeapConfigurationVK> /*p_configurations*/) {
}

void DescriptorSetPluginVK::create_descriptor_arenas(RenderContextVK* /*p_render_data*/) {
}

void DescriptorSetPluginVK::on_frame_begin(const RenderContextVK* /*p_render_data*/) {
}

void DescriptorSetPluginVK::on_render_graph_registered(std::string_view /*p_graph_key*/, const RenderGraphVK& /*p_graph*/) {
}

void DescriptorSetPluginVK::on_before_task_recorded(VkCommandBuffer /*p_command_buffer*/,
    const RenderGraphVK& /*p_graph*/,
    const RendTaskID& /*p_task_id*/,
    QueueFamilyVK /*p_queue_family*/) {
}

void DescriptorSetPluginVK::on_after_task_recorded(VkCommandBuffer /*p_command_buffer*/,
    const RenderGraphVK& /*p_graph*/,
    const RendTaskID& /*p_task_id*/,
    QueueFamilyVK /*p_queue_family*/) {
}

void DescriptorSetPluginVK::on_renderer_terminating() {
    destroy_descriptor_pool();
}

void DescriptorSetPluginVK::destroy_descriptor_pool() {
    if (descriptor_pool == VK_NULL_HANDLE) {
        return;
    }
    vkDestroyDescriptorPool(logical_device, descriptor_pool, render_data->allocation_callbacks);
    descriptor_pool = VK_NULL_HANDLE;
}

} // namespace WhiteBirdEngine

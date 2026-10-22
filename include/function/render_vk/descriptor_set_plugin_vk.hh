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
#ifndef WBE_FILE_DESCRIPTOR_SET_PLUGIN_VK_HH
#define WBE_FILE_DESCRIPTOR_SET_PLUGIN_VK_HH

#include "function/render/i_render_task.hh"
#include "function/render/i_renderer.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_plugin_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "utils/interface/i_singleton.hh"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class DescriptorSetPluginVK
 * @brief Vulkan renderer plugin that provides classic descriptor sets next to the renderer's descriptor heaps.
 *
 * Engine pipelines access resources through the renderer-owned descriptor heaps. Code that cannot, such as third-party
 * libraries recording their own pipelines with pipeline layouts, allocates descriptor sets from the descriptor pool this
 * plugin owns when RendererCapabilities::descriptor_set is enabled. Pipelines created without
 * VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT may bind these sets after the render graph has bound the descriptor heaps.
 *
 * The pool is created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, holds at most MAX_DESCRIPTOR_SETS sets, and
 * DESCRIPTORS_PER_TYPE descriptors of every classic descriptor type. It exists from device creation until the renderer
 * terminates, which destroys every set still allocated from it. Callers free sets only after the GPU work using them has
 * completed.
 *
 * The plugin is owned by the renderer, is neither copyable nor movable, and is confined to the rendering thread.
 */
class DescriptorSetPluginVK final : public RenderPluginVK, public ISingleton<DescriptorSetPluginVK> {
public:
    /**
     * @brief The maximum number of descriptor sets allocated from the pool at once.
     */
    static constexpr uint32_t MAX_DESCRIPTOR_SETS = 1024;
    /**
     * @brief The number of descriptors of every descriptor type the pool holds.
     */
    static constexpr uint32_t DESCRIPTORS_PER_TYPE = 1024;

    DescriptorSetPluginVK() = delete;
    virtual ~DescriptorSetPluginVK() override;
    DescriptorSetPluginVK(const DescriptorSetPluginVK&) = delete;
    DescriptorSetPluginVK(DescriptorSetPluginVK&&) noexcept = delete;
    DescriptorSetPluginVK& operator=(const DescriptorSetPluginVK&) = delete;
    DescriptorSetPluginVK& operator=(DescriptorSetPluginVK&&) noexcept = delete;

    /**
     * @brief Construct the descriptor-set plugin.
     *
     * @param p_render_data Renderer data that outlives the plugin.
     */
    explicit DescriptorSetPluginVK(const RenderContextVK* p_render_data);

    /**
     * @brief Get the singleton descriptor-set plugin instance.
     *
     * @return The singleton instance, or null when no renderer owns the plugin.
     */
    static DescriptorSetPluginVK* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the Vulkan instance of the renderer, which descriptor-set based libraries commonly require.
     *
     * @return The Vulkan instance.
     * @throws std::runtime_error If the instance has not been created.
     */
    VkInstance get_instance() const;

    /**
     * @brief Get the plugin-owned descriptor pool.
     *
     * @return The descriptor pool, valid until the renderer terminates.
     * @throws std::runtime_error If the pool has not been created or has been destroyed.
     */
    VkDescriptorPool get_descriptor_pool() const;

    /**
     * @brief Allocate one descriptor set from the plugin-owned descriptor pool.
     *
     * @param p_layout The layout of the descriptor set; it must stay alive while the set is used.
     * @return The descriptor set. Release it with free_descriptor_set(), or leave it to the renderer's termination.
     * @throws std::runtime_error If the pool is unavailable or the allocation fails.
     */
    VkDescriptorSet allocate_descriptor_set(VkDescriptorSetLayout p_layout);

    /**
     * @brief Return a descriptor set allocated by allocate_descriptor_set() to the pool.
     *
     * @param p_descriptor_set The descriptor set; no pending GPU work may use it.
     * @throws std::runtime_error If the pool is unavailable or the set cannot be freed.
     */
    void free_descriptor_set(VkDescriptorSet p_descriptor_set);

    /**
     * @brief Enable the plugin when RendererCapabilities::descriptor_set is requested.
     *
     * Descriptor sets are core Vulkan, so a requested capability is always enabled.
     */
    virtual bool try_enable(RendererCapabilities& p_capabilities,
        VkPhysicalDevice p_physical_device,
        const char* const* p_enabled_layers,
        size_t p_enabled_layer_count) override;

    virtual bool is_enabled() const override {
        return enabled;
    }

    virtual std::span<const char* const> get_required_instance_extensions() const override;
    virtual std::span<const char* const> get_required_device_extensions() const override;
    virtual void on_before_instance_creation(VkInstanceCreateInfo* p_create_info) override;

    /**
     * @brief Retain the Vulkan instance for get_instance().
     */
    virtual void on_after_instance_created(VkInstance p_instance) override;

    virtual void on_before_device_creation(
        VkPhysicalDevice p_physical_device, VkDeviceCreateInfo* p_create_info, VkPhysicalDeviceFeatures* p_device_features) override;

    /**
     * @brief Create the descriptor pool.
     *
     * @throws std::runtime_error If the descriptor pool cannot be created.
     */
    virtual void on_after_device_created(
        RenderContextVK* p_render_data, VkPhysicalDevice p_physical_device, VkDevice p_logical_device) override;

    virtual void update_render_context(RenderContextVK* p_render_data, VkPhysicalDevice p_physical_device) override;
    virtual void configure_descriptor_heaps(
        const RenderContextVK* p_render_data, std::span<DescriptorHeapConfigurationVK> p_configurations) override;
    virtual void create_descriptor_arenas(RenderContextVK* p_render_data) override;
    virtual void on_frame_begin(const RenderContextVK* p_render_data) override;
    virtual void on_render_graph_registered(std::string_view p_graph_key, const RenderGraphVK& p_graph) override;
    virtual void on_before_task_recorded(VkCommandBuffer p_command_buffer,
        const RenderGraphVK& p_graph,
        const RendTaskID& p_task_id,
        QueueFamilyVK p_queue_family) override;
    virtual void on_after_task_recorded(VkCommandBuffer p_command_buffer,
        const RenderGraphVK& p_graph,
        const RendTaskID& p_task_id,
        QueueFamilyVK p_queue_family) override;

    /**
     * @brief Destroy the descriptor pool together with every set still allocated from it.
     */
    virtual void on_renderer_terminating() override;

private:
    const RenderContextVK* render_data = nullptr;
    VkInstance vk_instance = VK_NULL_HANDLE;
    VkDevice logical_device = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    bool enabled = false;

    void destroy_descriptor_pool();

    inline static DescriptorSetPluginVK* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif

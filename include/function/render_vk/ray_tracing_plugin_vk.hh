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
#ifndef WBE_FILE_RAY_TRACING_PLUGIN_VK_HH
#define WBE_FILE_RAY_TRACING_PLUGIN_VK_HH

#include "core/memory/unique.hh"
#include "function/render/i_render_task.hh"
#include "function/render/i_renderer.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_plugin_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "utils/interface/i_singleton.hh"

#include <cstddef>
#include <span>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RayTracingPluginVK
 * @brief Vulkan renderer plugin that enables ray tracing and owns related shared resources.
 *
 * Ray-tracing state is neither copyable nor movable.
 */
class RayTracingPluginVK final : public RenderPluginVK, public ISingleton<RayTracingPluginVK> {
public:
    RayTracingPluginVK() = delete;
    virtual ~RayTracingPluginVK() override;
    RayTracingPluginVK(const RayTracingPluginVK&) = delete;
    RayTracingPluginVK(RayTracingPluginVK&&) noexcept = delete;
    RayTracingPluginVK& operator=(const RayTracingPluginVK&) = delete;
    RayTracingPluginVK& operator=(RayTracingPluginVK&&) noexcept = delete;

    /**
     * @brief Construct ray-tracing helper state.
     *
     * @param p_render_data Renderer data used for Vulkan resource creation.
     */
    explicit RayTracingPluginVK(const RenderContextVK* p_render_data);

    /**
     * @brief Get the singleton ray-tracing plugin instance.
     *
     * @return The singleton instance, or null when no renderer owns the plugin.
     */
    static RayTracingPluginVK* get_singleton() {
        return singleton;
    }

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
    virtual void on_after_instance_created(VkInstance p_instance) override;
    virtual void on_before_device_creation(
        VkPhysicalDevice p_physical_device, VkDeviceCreateInfo* p_create_info, VkPhysicalDeviceFeatures* p_device_features) override;

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
    virtual void on_renderer_terminating() override;

    /**
     * @brief Return the ray-tracing device state, for creating acceleration structures.
     * @return Plugin-owned state valid while the plugin lives; its device commands are loaded once the device exists.
     */
    const BaseRayTracingDataVK* get_base_ray_tracing_data() const {
        return base_ray_tracing_data.get();
    }

private:
    VkInstance vk_instance = VK_NULL_HANDLE;
    VkDeviceSize acceleration_structure_descriptor_byte_offset = 0;
    Unique<BaseRayTracingDataVK> base_ray_tracing_data;
    bool enabled = false;
    bool ray_query_enabled = false;
    bool ray_tracing_pipeline_enabled = false;
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features{};
    VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features{};

    bool supports_base_features(VkPhysicalDevice p_physical_device, const char* const* p_enabled_layers, size_t p_enabled_layer_count) const;
    bool supports_ray_query(VkPhysicalDevice p_physical_device, const char* const* p_enabled_layers, size_t p_enabled_layer_count) const;
    bool supports_ray_tracing_pipeline(
        VkPhysicalDevice p_physical_device, const char* const* p_enabled_layers, size_t p_enabled_layer_count) const;
    void prepare_device_features(const RendererCapabilities& p_capabilities);

    inline static RayTracingPluginVK* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif

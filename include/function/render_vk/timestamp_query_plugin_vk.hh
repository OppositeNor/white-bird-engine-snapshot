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
#ifndef WBE_FILE_TIMESTAMP_QUERY_PLUGIN_VK_HH
#define WBE_FILE_TIMESTAMP_QUERY_PLUGIN_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render/i_render_task.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_metadata_vk.hh"
#include "function/render_vk/render_plugin_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "platform/file_system/path.hh"
#include "utils/interface/i_singleton.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class TimestampQueryPluginVK
 * @brief Vulkan renderer plugin that samples render-graph task GPU times with timestamp queries.
 *
 * When enabled through RendererCapabilities::timestamp_query, the plugin brackets every render-graph task's command buffer
 * with timestamps, reads them back once the frame slot's work has completed, and accumulates per-task and per-queue
 * statistics. Task statistics are kept per registered render graph, so graphs that share task identifiers are timed
 * separately. The plugin also takes a RendGraphMetadataVK snapshot of every graph registered with the renderer. When the
 * renderer terminates it writes the statistics, and every registered graph's tasks and edges together with each task's
 * statistics, as a JSON report to get_report_path().
 *
 * The plugin is owned by the renderer, is neither copyable nor movable, and is confined to the rendering thread.
 */
class TimestampQueryPluginVK final : public RenderPluginVK, public ISingleton<TimestampQueryPluginVK> {
public:
    /**
     * @brief The maximum number of render-graph tasks sampled in one frame.
     */
    static constexpr uint32_t MAX_TASKS_PER_FRAME = 256;
    /**
     * @brief The report file name used, inside the executable directory, until set_report_path() is called.
     */
    static constexpr std::string_view DEFAULT_REPORT_FILE_NAME = "gpu_timestamp_report.json";

    TimestampQueryPluginVK() = delete;
    virtual ~TimestampQueryPluginVK() override;
    TimestampQueryPluginVK(const TimestampQueryPluginVK&) = delete;
    TimestampQueryPluginVK(TimestampQueryPluginVK&&) noexcept = delete;
    TimestampQueryPluginVK& operator=(const TimestampQueryPluginVK&) = delete;
    TimestampQueryPluginVK& operator=(TimestampQueryPluginVK&&) noexcept = delete;

    /**
     * @brief Construct the timestamp-query plugin.
     *
     * @param p_render_data Renderer data that outlives the plugin; its current frame slot selects the recorded queries.
     */
    explicit TimestampQueryPluginVK(const RenderContextVK* p_render_data);

    /**
     * @brief Get the singleton timestamp-query plugin instance.
     *
     * @return The singleton instance, or null when no renderer owns the plugin.
     */
    static TimestampQueryPluginVK* get_singleton() {
        return singleton;
    }

    /**
     * @brief Set the path the JSON report is written to when the renderer terminates.
     *
     * Missing parent directories are created when the report is written.
     *
     * @param p_report_path The report file path.
     */
    void set_report_path(Path p_report_path) {
        report_path = std::move(p_report_path);
    }

    /**
     * @brief Get the path the JSON report is written to when the renderer terminates.
     *
     * @return The report file path.
     */
    const Path& get_report_path() const {
        return report_path;
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

    /**
     * @brief Snapshot the graph's metadata for the report and attribute its tasks' timestamps to p_graph_key.
     *
     * A graph registered again under the same key replaces the previous snapshot; samples of both graphs are accumulated
     * under the key.
     */
    virtual void on_render_graph_registered(std::string_view p_graph_key, const RenderGraphVK& p_graph) override;

    /**
     * @brief Collect the completed timestamps of the frame slot that is starting, then prepare it for new queries.
     *
     * Blocks until every query previously recorded for the slot is available.
     *
     * @throws std::runtime_error If the query pool cannot be created or its results cannot be read.
     */
    virtual void on_frame_begin(const RenderContextVK* p_render_data) override;

    /**
     * @brief Reset the task's query pair and write its start timestamp.
     *
     * @throws std::runtime_error If the frame slot has not begun or the frame exceeds MAX_TASKS_PER_FRAME tasks.
     */
    virtual void on_before_task_recorded(VkCommandBuffer p_command_buffer,
        const RenderGraphVK& p_graph,
        const RendTaskID& p_task_id,
        QueueFamilyVK p_queue_family) override;

    /**
     * @brief Write the task's end timestamp after all of its commands.
     */
    virtual void on_after_task_recorded(VkCommandBuffer p_command_buffer,
        const RenderGraphVK& p_graph,
        const RendTaskID& p_task_id,
        QueueFamilyVK p_queue_family) override;

    /**
     * @brief Collect all outstanding timestamps, write the JSON report, and destroy the query pools.
     *
     * @throws std::runtime_error If query results cannot be read or the report cannot be written.
     */
    virtual void on_renderer_terminating() override;

private:
    static constexpr size_t QUEUE_FAMILY_COUNT = 3;

    // Running duration statistics in nanoseconds, accumulated with Welford's algorithm.
    struct DurationStatistics {
        uint64_t sample_count = 0;
        double total = 0.0;
        double mean = 0.0;
        double squared_deviation_sum = 0.0;
        double min = 0.0;
        double max = 0.0;

        void add_sample(double p_duration);
        double get_standard_deviation() const;
    };

    struct RecordedTaskQuery {
        // The key of the graph the task belongs to; empty for graphs not registered with the renderer.
        std::string graph_key;
        RendTaskID task_id;
        QueueFamilyVK queue_family = QueueFamilyVK::GRAPHICS;
    };

    struct RegisteredGraphMetadata {
        std::string graph_key;
        RendGraphMetadataVK metadata;
    };

    // Queries of one frame-in-flight slot. Task i owns queries 2i (start) and 2i + 1 (end).
    struct FrameQueries {
        VkQueryPool query_pool = VK_NULL_HANDLE;
        Vector<RecordedTaskQuery, RenderAllocator> tasks;
        bool task_open = false;
    };

    using TaskKey = std::tuple<std::string, QueueFamilyVK, RendTaskID>;

    const RenderContextVK* render_data = nullptr;
    VkDevice logical_device = VK_NULL_HANDLE;
    bool enabled = false;
    double timestamp_period = 0.0;
    uint32_t timestamp_valid_bits = 0;
    uint64_t timestamp_mask = 0;
    std::array<char, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE> device_name{};
    Path report_path;

    Vector<FrameQueries, RenderAllocator> frame_queries;
    Vector<uint64_t, RenderAllocator> query_results;
    Map<TaskKey, DurationStatistics, RenderAllocator> task_statistics;
    // The registration key of every graph registered with the renderer.
    Map<const RenderGraphVK*, std::string, RenderAllocator> graph_keys;
    // The metadata of every registered graph key, in registration order.
    Vector<RegisteredGraphMetadata, RenderAllocator> graph_metadata;
    std::array<DurationStatistics, QUEUE_FAMILY_COUNT> queue_frame_statistics{};
    uint64_t sampled_frame_count = 0;

    void prepare_frame_queries(uint32_t p_frame_count);
    FrameQueries& get_recording_frame_queries();
    void collect_frame_queries(FrameQueries& p_frame_queries);
    std::string_view get_graph_key(const RenderGraphVK& p_graph) const;
    void write_report() const;
    void destroy_query_pools();

    inline static TimestampQueryPluginVK* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif

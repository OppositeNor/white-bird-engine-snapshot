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
#include "function/render_vk/timestamp_query_plugin_vk.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/logging/log.hh"
#include "function/render/i_render_task.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_metadata_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "platform/file_system/file_system.hh"
#include "platform/file_system/path.hh"
#include "utils/defs.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

constexpr double NANOSECONDS_PER_MILLISECOND = 1'000'000.0;

const char* queue_family_name(QueueFamilyVK p_queue_family) {
    switch (p_queue_family) {
    case QueueFamilyVK::GRAPHICS:
        return "graphics";
    case QueueFamilyVK::COMPUTE_1:
        return "compute_1";
    case QueueFamilyVK::COMPUTE_2:
        return "compute_2";
    }
    throw std::runtime_error("Failed to name queue family: unknown queue family.");
}

} // namespace

void TimestampQueryPluginVK::DurationStatistics::add_sample(double p_duration) {
    if (sample_count == 0) {
        min = p_duration;
        max = p_duration;
    } else {
        min = std::min(min, p_duration);
        max = std::max(max, p_duration);
    }
    ++sample_count;
    total += p_duration;
    const double delta = p_duration - mean;
    mean += delta / static_cast<double>(sample_count);
    squared_deviation_sum += delta * (p_duration - mean);
}

double TimestampQueryPluginVK::DurationStatistics::get_standard_deviation() const {
    if (sample_count < 2) {
        return 0.0;
    }
    return std::sqrt(squared_deviation_sum / static_cast<double>(sample_count - 1));
}

TimestampQueryPluginVK::TimestampQueryPluginVK(const RenderContextVK* p_render_data)
    : render_data(p_render_data), report_path(FileSystem::get_executable_dir() / DEFAULT_REPORT_FILE_NAME),
      frame_queries(p_render_data->allocator), query_results(p_render_data->allocator), task_statistics(p_render_data->allocator),
      graph_keys(p_render_data->allocator), graph_metadata(p_render_data->allocator) {
    singleton = this;
}

TimestampQueryPluginVK::~TimestampQueryPluginVK() {
    // Pools are released by on_renderer_terminating(); this only covers renderers that failed before termination.
    destroy_query_pools();
    singleton = nullptr;
}

bool TimestampQueryPluginVK::try_enable(
    RendererCapabilities& p_capabilities, VkPhysicalDevice p_physical_device, const char* const* /*p_enabled_layers*/, size_t /*p_enabled_layer_count*/) {
    enabled = false;
    if (!p_capabilities.timestamp_query) {
        return false;
    }

    // timestampComputeAndGraphics guarantees timestamps on every graphics and compute queue the graph submits to.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(p_physical_device, &properties);
    if (properties.limits.timestampComputeAndGraphics != VK_TRUE || properties.limits.timestampPeriod <= 0.0F) {
        stdout_log(WBE_CHANNEL_RENDER)->warning("Timestamp queries are not supported on graphics and compute queues; timestamp_query is disabled.");
        p_capabilities.timestamp_query = false;
        return false;
    }

    // Durations are computed modulo the narrowest timestamp counter of the queues that may run graph tasks.
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(p_physical_device, &queue_family_count, nullptr);
    Vector<VkQueueFamilyProperties, RenderAllocator> queue_families(queue_family_count, render_data->allocator);
    vkGetPhysicalDeviceQueueFamilyProperties(p_physical_device, &queue_family_count, queue_families.data());
    uint32_t valid_bits = std::numeric_limits<uint32_t>::max();
    for (const auto& queue_family : queue_families) {
        if ((queue_family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0 && queue_family.timestampValidBits != 0) {
            valid_bits = std::min(valid_bits, queue_family.timestampValidBits);
        }
    }
    if (valid_bits == std::numeric_limits<uint32_t>::max()) {
        stdout_log(WBE_CHANNEL_RENDER)->warning("No graphics or compute queue family has valid timestamp bits; timestamp_query is disabled.");
        p_capabilities.timestamp_query = false;
        return false;
    }

    timestamp_period = static_cast<double>(properties.limits.timestampPeriod);
    timestamp_valid_bits = valid_bits;
    timestamp_mask = valid_bits >= 64 ? std::numeric_limits<uint64_t>::max() : ((uint64_t{1} << valid_bits) - 1);
    std::ranges::copy(properties.deviceName, device_name.begin());
    device_name.back() = '\0';
    enabled = true;
    return true;
}

std::span<const char* const> TimestampQueryPluginVK::get_required_instance_extensions() const {
    return {};
}

std::span<const char* const> TimestampQueryPluginVK::get_required_device_extensions() const {
    // Timestamp queries and vkCmdWriteTimestamp2 are core in the renderer's Vulkan version.
    return {};
}

void TimestampQueryPluginVK::on_before_instance_creation(VkInstanceCreateInfo* /*p_create_info*/) {
}

void TimestampQueryPluginVK::on_after_instance_created(VkInstance /*p_instance*/) {
}

void TimestampQueryPluginVK::on_before_device_creation(
    VkPhysicalDevice /*p_physical_device*/, VkDeviceCreateInfo* /*p_create_info*/, VkPhysicalDeviceFeatures* /*p_device_features*/) {
}

void TimestampQueryPluginVK::on_after_device_created(
    RenderContextVK* /*p_render_data*/, VkPhysicalDevice /*p_physical_device*/, VkDevice p_logical_device) {
    WBE_DEBUG_ASSERT(enabled);
    logical_device = p_logical_device;
}

void TimestampQueryPluginVK::update_render_context(RenderContextVK* /*p_render_data*/, VkPhysicalDevice /*p_physical_device*/) {
}

void TimestampQueryPluginVK::configure_descriptor_heaps(
    const RenderContextVK* /*p_render_data*/, std::span<DescriptorHeapConfigurationVK> /*p_configurations*/) {
}

void TimestampQueryPluginVK::create_descriptor_arenas(RenderContextVK* /*p_render_data*/) {
}

void TimestampQueryPluginVK::on_render_graph_registered(std::string_view p_graph_key, const RenderGraphVK& p_graph) {
    WBE_DEBUG_ASSERT(enabled);
    graph_keys[&p_graph] = std::string(p_graph_key);
    RendGraphMetadataVK metadata = p_graph.dump_graph_metadata();
    auto existing = std::ranges::find_if(graph_metadata, [p_graph_key](const RegisteredGraphMetadata& p_registered) {
        return p_registered.graph_key == p_graph_key;
    });
    if (existing != graph_metadata.end()) {
        existing->metadata = std::move(metadata);
        return;
    }
    graph_metadata.push_back({.graph_key = std::string(p_graph_key), .metadata = std::move(metadata)});
}

void TimestampQueryPluginVK::on_frame_begin(const RenderContextVK* p_render_data) {
    WBE_DEBUG_ASSERT(enabled);
    WBE_DEBUG_ASSERT(p_render_data == render_data);
    // Swap-chain recreation may raise the number of frames in flight, so pools are created on demand.
    prepare_frame_queries(p_render_data->frames_in_flight);
    collect_frame_queries(frame_queries[p_render_data->current_frame_slot]);
}

void TimestampQueryPluginVK::on_before_task_recorded(
    VkCommandBuffer p_command_buffer, const RenderGraphVK& p_graph, const RendTaskID& p_task_id, QueueFamilyVK p_queue_family) {
    WBE_DEBUG_ASSERT(enabled);
    FrameQueries& frame = get_recording_frame_queries();
    WBE_DEBUG_ASSERT(!frame.task_open);
    if (frame.tasks.size() >= MAX_TASKS_PER_FRAME) {
        throw std::runtime_error(
            std::format("Failed to record GPU timestamps: a frame records more than {} render-graph tasks.", MAX_TASKS_PER_FRAME));
    }
    const auto first_query = static_cast<uint32_t>(frame.tasks.size() * 2);
    frame.tasks.push_back(
        {.graph_key = std::string(get_graph_key(p_graph)), .task_id = p_task_id, .queue_family = p_queue_family});
    frame.task_open = true;
    // The reset and both writes are query commands on the same queue, so they execute in submission order.
    vkCmdResetQueryPool(p_command_buffer, frame.query_pool, first_query, 2);
    vkCmdWriteTimestamp2(p_command_buffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.query_pool, first_query);
}

void TimestampQueryPluginVK::on_after_task_recorded(VkCommandBuffer p_command_buffer,
    [[maybe_unused]] const RenderGraphVK& p_graph,
    [[maybe_unused]] const RendTaskID& p_task_id,
    [[maybe_unused]] QueueFamilyVK p_queue_family) {
    WBE_DEBUG_ASSERT(enabled);
    FrameQueries& frame = get_recording_frame_queries();
    WBE_DEBUG_ASSERT(frame.task_open);
    WBE_DEBUG_ASSERT(frame.tasks.back().task_id == p_task_id && frame.tasks.back().queue_family == p_queue_family &&
                     frame.tasks.back().graph_key == get_graph_key(p_graph));
    const auto end_query = static_cast<uint32_t>(frame.tasks.size() * 2 - 1);
    // The end timestamp is written once every previously recorded command of the task has completed.
    vkCmdWriteTimestamp2(p_command_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.query_pool, end_query);
    frame.task_open = false;
}

void TimestampQueryPluginVK::on_renderer_terminating() {
    WBE_DEBUG_ASSERT(enabled);
    for (auto& frame : frame_queries) {
        collect_frame_queries(frame);
    }
    write_report();
    destroy_query_pools();
}

void TimestampQueryPluginVK::prepare_frame_queries(uint32_t p_frame_count) {
    WBE_DEBUG_ASSERT(logical_device != VK_NULL_HANDLE);
    while (frame_queries.size() < p_frame_count) {
        VkQueryPoolCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        create_info.queryCount = MAX_TASKS_PER_FRAME * 2;
        VkQueryPool query_pool = VK_NULL_HANDLE;
        VkResult result = vkCreateQueryPool(logical_device, &create_info, render_data->allocation_callbacks, &query_pool);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::format("Failed to create timestamp query pool: {}.", string_VkResult(result)));
        }
        frame_queries.push_back(FrameQueries{
            .query_pool = query_pool,
            .tasks = Vector<RecordedTaskQuery, RenderAllocator>(render_data->allocator),
            .task_open = false,
        });
    }
}

TimestampQueryPluginVK::FrameQueries& TimestampQueryPluginVK::get_recording_frame_queries() {
    if (render_data->current_frame_slot >= frame_queries.size()) {
        throw std::runtime_error("Failed to record GPU timestamps: the current frame slot has not begun.");
    }
    return frame_queries[render_data->current_frame_slot];
}

void TimestampQueryPluginVK::collect_frame_queries(FrameQueries& p_frame_queries) {
    WBE_DEBUG_ASSERT(!p_frame_queries.task_open);
    if (p_frame_queries.tasks.empty()) {
        return;
    }
    // Every recorded query has been submitted, so waiting cannot stall indefinitely. Compute-queue tasks are not covered by
    // the frame's in-flight fence, so the wait is required for them.
    const auto query_count = static_cast<uint32_t>(p_frame_queries.tasks.size() * 2);
    query_results.resize(query_count);
    VkResult result = vkGetQueryPoolResults(logical_device,
        p_frame_queries.query_pool,
        0,
        query_count,
        query_results.size() * sizeof(uint64_t),
        query_results.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to read GPU timestamp query results: {}.", string_VkResult(result)));
    }

    // Timestamps are only comparable within one queue, so frame spans are tracked per queue family.
    std::array<bool, QUEUE_FAMILY_COUNT> queue_used{};
    std::array<uint64_t, QUEUE_FAMILY_COUNT> queue_first_start{};
    std::array<uint64_t, QUEUE_FAMILY_COUNT> queue_last_end{};
    for (size_t i = 0; i < p_frame_queries.tasks.size(); ++i) {
        const auto& task = p_frame_queries.tasks[i];
        const uint64_t start = query_results[i * 2] & timestamp_mask;
        const uint64_t end = query_results[(i * 2) + 1] & timestamp_mask;
        const auto duration = static_cast<double>((end - start) & timestamp_mask) * timestamp_period;
        task_statistics[TaskKey(task.graph_key, task.queue_family, task.task_id)].add_sample(duration);

        const auto queue_index = static_cast<size_t>(std::to_underlying(task.queue_family));
        WBE_DEBUG_ASSERT(queue_index < QUEUE_FAMILY_COUNT);
        if (!queue_used[queue_index]) {
            queue_used[queue_index] = true;
            queue_first_start[queue_index] = start;
            queue_last_end[queue_index] = end;
        } else {
            queue_first_start[queue_index] = std::min(queue_first_start[queue_index], start);
            queue_last_end[queue_index] = std::max(queue_last_end[queue_index], end);
        }
    }
    for (size_t i = 0; i < QUEUE_FAMILY_COUNT; ++i) {
        if (queue_used[i]) {
            const auto span = static_cast<double>((queue_last_end[i] - queue_first_start[i]) & timestamp_mask) * timestamp_period;
            queue_frame_statistics[i].add_sample(span);
        }
    }
    ++sampled_frame_count;
    p_frame_queries.tasks.clear();
}

std::string_view TimestampQueryPluginVK::get_graph_key(const RenderGraphVK& p_graph) const {
    auto entry = graph_keys.find(&p_graph);
    // Graphs may be built and executed without registering them with the renderer; their tasks are reported under "".
    if (entry == graph_keys.end()) {
        return {};
    }
    return entry->second;
}

void TimestampQueryPluginVK::write_report() const {
    auto statistics_json = [](const DurationStatistics& p_statistics) {
        nlohmann::json json;
        json["samples"] = p_statistics.sample_count;
        json["mean_ms"] = p_statistics.mean / NANOSECONDS_PER_MILLISECOND;
        json["min_ms"] = p_statistics.min / NANOSECONDS_PER_MILLISECOND;
        json["max_ms"] = p_statistics.max / NANOSECONDS_PER_MILLISECOND;
        json["stddev_ms"] = p_statistics.get_standard_deviation() / NANOSECONDS_PER_MILLISECOND;
        json["total_ms"] = p_statistics.total / NANOSECONDS_PER_MILLISECOND;
        return json;
    };

    nlohmann::json report;
    report["device"] = std::string(device_name.data());
    report["timestamp_period_ns"] = timestamp_period;
    report["timestamp_valid_bits"] = timestamp_valid_bits;
    report["sampled_frames"] = sampled_frame_count;

    // Frame spans run from the first task start to the last task end on each queue.
    report["queues"] = nlohmann::json::array();
    for (size_t i = 0; i < QUEUE_FAMILY_COUNT; ++i) {
        if (queue_frame_statistics[i].sample_count == 0) {
            continue;
        }
        nlohmann::json queue = statistics_json(queue_frame_statistics[i]);
        queue["queue"] = queue_family_name(static_cast<QueueFamilyVK>(i));
        report["queues"].push_back(std::move(queue));
    }

    // List the most expensive tasks first.
    Vector<const std::pair<TaskKey, DurationStatistics>*, RenderAllocator> sorted_tasks(render_data->allocator);
    sorted_tasks.reserve(task_statistics.size());
    for (const auto& entry : task_statistics) {
        sorted_tasks.push_back(&entry);
    }
    std::ranges::stable_sort(sorted_tasks, [](const auto* p_left, const auto* p_right) {
        return p_left->second.total > p_right->second.total;
    });
    report["tasks"] = nlohmann::json::array();
    for (const auto* entry : sorted_tasks) {
        nlohmann::json task = statistics_json(entry->second);
        task["render_graph"] = std::get<0>(entry->first);
        task["task_id"] = std::get<2>(entry->first);
        task["queue"] = queue_family_name(std::get<1>(entry->first));
        report["tasks"].push_back(std::move(task));
    }

    // Every registered graph with its tasks in execution order, each with its statistics (null when never sampled).
    report["render_graphs"] = nlohmann::json::array();
    for (const RegisteredGraphMetadata& registered : graph_metadata) {
        nlohmann::json graph;
        graph["key"] = registered.graph_key;
        graph["tasks"] = nlohmann::json::array();
        for (const RendGraphTaskMetadataVK& task_metadata : registered.metadata.tasks) {
            nlohmann::json task;
            task["task_id"] = task_metadata.task_id;
            task["queue"] = queue_family_name(task_metadata.queue_family);
            task["topological_order"] = task_metadata.topological_order;
            task["subgraph"] = task_metadata.subgraph_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(task_metadata.subgraph_id);
            auto statistics =
                task_statistics.find(TaskKey(registered.graph_key, task_metadata.queue_family, task_metadata.task_id));
            task["timing"] = statistics == task_statistics.end() ? nlohmann::json(nullptr) : statistics_json(statistics->second);
            graph["tasks"].push_back(std::move(task));
        }
        graph["edges"] = nlohmann::json::array();
        for (const RendGraphEdgeMetadataVK& edge_metadata : registered.metadata.edges) {
            nlohmann::json edge;
            edge["from"] = edge_metadata.from_task_id;
            edge["to"] = edge_metadata.to_task_id;
            edge["resource"] = edge_metadata.resource_id;
            edge["from_task_writes"] = edge_metadata.from_task_writes;
            edge["to_task_writes"] = edge_metadata.to_task_writes;
            edge["cross_queue"] = edge_metadata.cross_queue;
            edge["src_stage"] = string_VkPipelineStageFlags2(edge_metadata.src_stage);
            edge["src_access"] = string_VkAccessFlags2(edge_metadata.src_access);
            edge["dst_stage"] = string_VkPipelineStageFlags2(edge_metadata.dst_stage);
            edge["dst_access"] = string_VkAccessFlags2(edge_metadata.dst_access);
            graph["edges"].push_back(std::move(edge));
        }
        // Each subgraph after the one containing it; its members name it as their subgraph.
        graph["subgraphs"] = nlohmann::json::array();
        for (const RendGraphSubgraphMetadataVK& subgraph_metadata : registered.metadata.subgraphs) {
            nlohmann::json subgraph;
            subgraph["id"] = subgraph_metadata.subgraph_id;
            subgraph["parent"] = subgraph_metadata.parent_subgraph_id.empty() ? nlohmann::json(nullptr)
                                                                               : nlohmann::json(subgraph_metadata.parent_subgraph_id);
            graph["subgraphs"].push_back(std::move(subgraph));
        }
        report["render_graphs"].push_back(std::move(graph));
    }

    if (report_path.has_parent_path()) {
        std::filesystem::create_directories(report_path.parent_path());
    }
    std::ofstream output(report_path);
    output << report.dump(4) << '\n';
    if (!output.good()) {
        throw std::runtime_error(std::format("Failed to write GPU timestamp report: {}.", report_path.generic_string()));
    }
    stdout_log(WBE_CHANNEL_RENDER)
        ->message(std::format(
            "GPU timestamp report of {} sampled frames written to {}.", sampled_frame_count, report_path.generic_string()));
}

void TimestampQueryPluginVK::destroy_query_pools() {
    for (auto& frame : frame_queries) {
        vkDestroyQueryPool(logical_device, frame.query_pool, render_data->allocation_callbacks);
    }
    frame_queries.clear();
}

} // namespace WhiteBirdEngine

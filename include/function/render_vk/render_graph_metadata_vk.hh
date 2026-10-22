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
#ifndef WBE_FILE_RENDER_GRAPH_METADATA_VK_HH
#define WBE_FILE_RENDER_GRAPH_METADATA_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render/i_render_task.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"

#include <cstdint>
#include <string>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RendGraphTaskMetadataVK
 * @brief One task of a compiled render graph.
 */
struct RendGraphTaskMetadataVK {
    /** The task identifier. */
    RendTaskID task_id;
    /** The queue family the task is submitted to. */
    QueueFamilyVK queue_family = QueueFamilyVK::GRAPHICS;
    /** The position of the task in the graph's execution order, starting at 0. */
    uint64_t topological_order = 0;
    /** The innermost subgraph the task belongs to; empty when it belongs to none. */
    RendTaskID subgraph_id;
};

/**
 * @class RendGraphSubgraphMetadataVK
 * @brief One subgraph of a compiled render graph; see RendSubgraphVK.
 */
struct RendGraphSubgraphMetadataVK {
    /** The subgraph identifier, which prefixes its members' ids. */
    RendTaskID subgraph_id;
    /** The subgraph that directly contains this one; empty for a subgraph of the graph itself. */
    RendTaskID parent_subgraph_id;
};

/**
 * @class RendGraphEdgeMetadataVK
 * @brief One resource dependency edge of a compiled render graph.
 * @details The stages and accesses are the ones the graph derived from the usages both tasks report for the resource.
 */
struct RendGraphEdgeMetadataVK {
    /** The task that uses the resource first. */
    RendTaskID from_task_id;
    /** The task that uses the resource next. */
    RendTaskID to_task_id;
    /** The graph resource identifier carried by the edge; aggregate resource lists are expanded to their entries. */
    std::string resource_id;
    /** The pipeline stages at which the previous task uses the resource. */
    VkPipelineStageFlags2 src_stage = VK_PIPELINE_STAGE_2_NONE;
    /** The writes of the previous task that are made available to the next task. */
    VkAccessFlags2 src_access = VK_ACCESS_2_NONE;
    /** The pipeline stages at which the next task uses the resource. */
    VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_NONE;
    /** The accesses of the next task that see the previous task's writes. */
    VkAccessFlags2 dst_access = VK_ACCESS_2_NONE;
    /** Whether the previous task reports a write of the resource; otherwise it only reads it. */
    bool from_task_writes = false;
    /** Whether the next task reports a write of the resource; otherwise it only reads it. */
    bool to_task_writes = false;
    /** Whether the tasks run on different queues, so the edge is a timeline semaphore rather than a pipeline barrier. */
    bool cross_queue = false;
};

/**
 * @class RendGraphMetadataVK
 * @brief The tasks and resource dependency edges of a compiled render graph, for diagnostics and profiling reports.
 * @details A snapshot independent of the graph it was taken from. Copyable and movable; copies share the allocator.
 */
struct RendGraphMetadataVK {
    RendGraphMetadataVK() = delete;
    /**
     * @brief Construct empty metadata.
     * @param p_allocator Allocator of the task and edge lists; must outlive the metadata.
     */
    explicit RendGraphMetadataVK(RenderAllocator* p_allocator)
        : tasks(p_allocator), edges(p_allocator), subgraphs(p_allocator) {
    }
    ~RendGraphMetadataVK() = default;
    RendGraphMetadataVK(const RendGraphMetadataVK&) = default;
    RendGraphMetadataVK(RendGraphMetadataVK&&) noexcept = default;
    RendGraphMetadataVK& operator=(const RendGraphMetadataVK&) = default;
    RendGraphMetadataVK& operator=(RendGraphMetadataVK&&) noexcept = default;

    /** Every task of the graph, in execution order. */
    Vector<RendGraphTaskMetadataVK, RenderAllocator> tasks;
    /** Every resource dependency edge of the graph, grouped by the order in which their source tasks were added. */
    Vector<RendGraphEdgeMetadataVK, RenderAllocator> edges;
    /** Every subgraph of the graph, nested ones included, each after the subgraph that contains it. */
    Vector<RendGraphSubgraphMetadataVK, RenderAllocator> subgraphs;
};

} // namespace WhiteBirdEngine

#endif

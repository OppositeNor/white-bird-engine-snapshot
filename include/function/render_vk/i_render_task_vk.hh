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
#ifndef WBE_FILE_I_RENDER_TASK_VK_HH
#define WBE_FILE_I_RENDER_TASK_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/i_render_task.hh"
#include "function/render/render_defs.hh"
#include <cstdint>
#include <string>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class QueueFamilyVK
 * @brief Logical queues a Vulkan render task can request. The
 * RenderGraphVK maps these to the concrete VkQueue / VkCommandPool from
 * RenderContextVK.
 *
 * COMPUTE_1 and COMPUTE_2 are two queues of the compute queue family. They are distinct VkQueues when the family exposes
 * enough queues; otherwise they share one VkQueue and the graph orders their tasks with pipeline barriers. Reflected; the
 * lowercase aliases are the names used in render graph recipes.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) QueueFamilyVK : uint32_t {
    GRAPHICS WBE_META(WBE_SERIALIZATION_ALIASES(graphics)) = 0,
    COMPUTE_1 WBE_META(WBE_SERIALIZATION_ALIASES(compute_1)) = 1,
    COMPUTE_2 WBE_META(WBE_SERIALIZATION_ALIASES(compute_2)) = 2,
};

class RendResViewVK;

/**
 * @class RendResUsageVK
 * @brief Task-facing resource usage categories translated privately by the render graph into Vulkan synchronization scopes.
 */
enum class RendResUsageVK : uint32_t {
    /** Shader-sampled image read. */
    SAMPLED_IMAGE,
    /** Color attachment read or write. */
    COLOR_ATTACHMENT,
    /** Depth/stencil attachment read or write. */
    DEPTH_ATTACHMENT,
    /** Shader storage image read or write. */
    STORAGE_IMAGE,
    /** Uniform buffer read. */
    UNIFORM_BUFFER,
    /** Shader storage buffer read or write. */
    STORAGE_BUFFER,
    /** Acceleration-structure build write or shader read. */
    ACCELERATION_STRUCTURE,
    /** Transfer source read. */
    TRANSFER_SRC,
    /** Transfer destination write. */
    TRANSFER_DST,
    /** Indirect command buffer read. */
    INDIRECT_BUFFER,
    /** Vertex buffer read. */
    VERTEX_BUFFER,
    /** Index buffer read. */
    INDEX_BUFFER,
    /** Conservative read or write by any command at any stage, e.g. a task recording its own full memory barrier. */
    ALL_COMMANDS,
};

/**
 * @class ResAccessVK
 * @brief Compact resource access reported by a Vulkan render task through IRenderTaskVK::append_accesses().
 */
struct ResAccessVK {
    /** Resource identifier used by the graph. */
    std::string res_id;
    /** Task-facing usage translated internally to Vulkan access and stage masks. */
    RendResUsageVK usage = RendResUsageVK::SAMPLED_IMAGE;
};

/**
 * @class IRenderTaskVK
 * @brief Vulkan render task. A IRenderTaskVK records work into a single
 * Vulkan command buffer per frame; the owning RenderGraphVK schedules it
 * onto a queue family chosen by `queue_family()` and synchronises it with
 * other tasks through the graph's resource dependency edges.
 */
struct IRenderTaskVK : public IRenderTask {
    IRenderTaskVK() = default;
    virtual ~IRenderTaskVK() = default;
    IRenderTaskVK(const IRenderTaskVK&) = default;
    IRenderTaskVK(IRenderTaskVK&&) noexcept = default;
    IRenderTaskVK& operator=(const IRenderTaskVK&) = default;
    IRenderTaskVK& operator=(IRenderTaskVK&&) noexcept = default;

    /**
     * @brief The queue family this task should run on. The render graph uses
     * this to select the command pool and queue, and to decide whether a
     * resource edge becomes a pipeline barrier or a timeline semaphore.
     *
     * @return The queue family index.
     */
    virtual QueueFamilyVK queue_family() const = 0;

    /**
     * @brief Bind stable graph resource containers before the task is first recorded.
     *
     * Tasks retain the containers they use and resolve the current render object from each container during record().
     * The view resolves the task-local identifiers through the task's resource map; identifiers are not looked up while
     * recording.
     *
     * @param p_resources The task's view of the graph resources.
     */
    virtual void bind_resources(const RendResViewVK& p_resources) {
        static_cast<void>(p_resources);
    }

    /**
     * @brief Append every resource access this task performs.
     *
     * Accesses use the task-local identifiers the task binds: reflected shader resource ids, render targets, and ids passed
     * to its constructor. The graph resolves them through the task's resource map before validating and synchronizing them.
     * A resource that is both read and written is appended to both lists.
     *
     * @param p_reads Receives read accesses.
     * @param p_writes Receives write accesses.
     */
    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const = 0;

    /**
     * @brief Record this task's commands into the given command buffer. The
     * command buffer has already been begun by the graph and will be ended by
     * the graph after this call returns. Barriers and timeline-semaphore waits
     * required by incoming resource dependencies are emitted or attached by the
     * graph before this task's commands, together with required image layout
     * transitions.
     *
     * @param p_command_buffer The command buffer to record into.
     * @param p_delta     The frame delta in seconds.
     */
    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) = 0;
};

} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_MESH_DRAW_BATCH_PLANNER_VK_HH
#define WBE_FILE_MESH_DRAW_BATCH_PLANNER_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class MeshDrawBatchPlannerVK
 * @brief Groups mesh draws by pipeline variant, shared vertex layout, and buffer placement, independent of any shader.
 *
 * Stateless and copyable. Planning preserves supplied instance indices when reordering draws.
 */
class MeshDrawBatchPlannerVK final {
public:
    /**
     * @class Batch
     * @brief Copyable vertex layout and buffer placement of a contiguous range of indirect commands.
     */
    struct Batch {
        /**
         * @brief Construct an empty command range.
         */
        Batch() = default;
        /**
         * @brief Release owned vertex-input descriptions.
         */
        ~Batch() = default;
        /**
         * @brief Copy the command range and vertex-input descriptions.
         */
        Batch(const Batch&) = default;
        /**
         * @brief Move the command range and vertex-input descriptions.
         */
        Batch(Batch&&) noexcept = default;
        /**
         * @brief Copy the command range and vertex-input descriptions.
         */
        Batch& operator=(const Batch&) = default;
        /**
         * @brief Move the command range and vertex-input descriptions.
         */
        Batch& operator=(Batch&&) noexcept = default;
        /**
         * @brief Index of the first command in the result's command array.
         */
        uint32_t first_draw = 0;
        /**
         * @brief Number of commands submitted together.
         */
        uint32_t draw_count = 0;
        /**
         * @brief Vertex bindings of one of the batch's draws; every draw of the batch has the same layout. Views the storage
         * passed to build().
         */
        std::span<const RUVertexView> vertex_bindings;
        /**
         * @brief The vertex binary read by each vertex binding, parallel to vertex_bindings.
         */
        std::vector<RefRes<RUBinary>> vertex_binaries;
        /**
         * @brief The byte offset bound for each vertex binding, parallel to vertex_bindings; command vertex offsets are
         * relative to these bases.
         */
        std::vector<VkDeviceSize> vertex_buffer_offsets;
        /**
         * @brief Pipeline variant shared by every draw of the batch.
         */
        std::string pipeline_variant;
    };

    /**
     * @struct Draw
     * @brief One indexed draw of a submesh view; copyable and movable.
     */
    struct Draw {
        Draw() = default;
        ~Draw() = default;
        Draw(const Draw&) = default;
        Draw(Draw&&) noexcept = default;
        Draw& operator=(const Draw&) = default;
        Draw& operator=(Draw&&) noexcept = default;
        /**
         * @brief Vertex bindings of the drawn submesh; the viewed storage must outlive the build() call.
         */
        std::span<const RUVertexView> vertex_bindings;
        /**
         * @brief Uint32 indices drawn by the command.
         */
        RUIndexView indices;
        /**
         * @brief Number of instances drawn; zero skips the draw.
         */
        uint32_t instance_count = 0;
        /**
         * @brief firstInstance value addressing the draw's instance records.
         */
        uint32_t first_instance = 0;
        /**
         * @brief Pipeline variant of the drawn material; only draws with equal variants share a batch. The viewed storage
         * must outlive the build() call.
         */
        std::string_view pipeline_variant = MATERIAL_PIPELINE_VARIANT_DEFAULT;
    };

    /**
     * @class BuildResult
     * @brief Move-only command and batch storage; its allocator must outlive the result.
     */
    struct BuildResult {
        BuildResult() = delete;
        /**
         * @brief Construct empty storage owned by the supplied allocator.
         */
        explicit BuildResult(RenderAllocator* p_allocator);
        /**
         * @brief Release owned command and batch storage.
         */
        ~BuildResult() = default;
        BuildResult(const BuildResult&) = delete;
        BuildResult& operator=(const BuildResult&) = delete;
        /**
         * @brief Transfer storage and allocator ownership associations.
         */
        BuildResult(BuildResult&&) noexcept = default;
        /**
         * @brief Transfer storage and allocator ownership associations.
         */
        BuildResult& operator=(BuildResult&&) noexcept = default;
        /**
         * @brief Tightly packed indirect commands in batch order.
         */
        Vector<VkDrawIndexedIndirectCommand, RenderAllocator> commands;
        /**
         * @brief Batches in first-seen compatible-group order.
         */
        Vector<Batch, RenderAllocator> batches;
    };

    /**
     * @brief Construct a stateless planner.
     */
    MeshDrawBatchPlannerVK() = default;
    /**
     * @brief Destroy the stateless planner.
     */
    ~MeshDrawBatchPlannerVK() = default;
    /**
     * @brief Copy the stateless planner.
     */
    MeshDrawBatchPlannerVK(const MeshDrawBatchPlannerVK&) = default;
    /**
     * @brief Move the stateless planner.
     */
    MeshDrawBatchPlannerVK(MeshDrawBatchPlannerVK&&) noexcept = default;
    /**
     * @brief Copy the stateless planner.
     */
    MeshDrawBatchPlannerVK& operator=(const MeshDrawBatchPlannerVK&) = default;
    /**
     * @brief Move the stateless planner.
     */
    MeshDrawBatchPlannerVK& operator=(MeshDrawBatchPlannerVK&&) noexcept = default;

    /**
     * @brief Plan draws sharing prevalidated vertex and index binaries.
     *
     * Draws batch together when their vertex bindings have the same layout (strides, attribute roles, formats, and offsets)
     * and the same placement in the vertex binaries up to a shared first vertex. Draws without indices or instances are
     * skipped. Compatible groups preserve source order internally and split at the device's maximum indirect draw count.
     * @throws std::runtime_error When the draw limit is zero, or Vulkan draw/layout limits are exceeded.
     */
    BuildResult build(RenderAllocator* p_allocator, std::span<const Draw> p_draws, uint32_t p_max_draw_count) const;

    /**
     * @brief Plan drawable submeshes sharing prevalidated vertex and index binaries.
     *
     * First-instance entries correspond to input submeshes. Empty draws are skipped. Compatible groups
     * preserve source order internally and split at the device's maximum indirect draw count.
     * @throws std::runtime_error When spans differ, the draw limit is zero, or Vulkan draw/layout limits are exceeded.
     */
    BuildResult build(RenderAllocator* p_allocator,
        std::span<const RUSubmesh3D> p_submeshes,
        std::span<const uint32_t> p_first_instances,
        uint32_t p_max_draw_count) const;
};

} // namespace WhiteBirdEngine

#endif

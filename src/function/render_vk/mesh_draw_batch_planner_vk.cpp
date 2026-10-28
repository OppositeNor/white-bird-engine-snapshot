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
#include "function/render_vk/mesh_draw_batch_planner_vk.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// The placement of one draw's vertices in a vertex binding, expressed in whole vertices of that binding.
struct MeshDrawBindingPlacementVK {
    RefRes<RUBinary> binary = MEM_NULL;
    size_t vertex_stride = 0;
    // Byte offset of the binding's first vertex inside a whole vertex.
    VkDeviceSize residue = 0;
    // First vertex of the draw minus the first vertex of the reference binding.
    int64_t vertex_delta = 0;
};

// The placement of one draw's vertices in every vertex binding. Draws batch together when their placements match up to a
// shared first vertex, which the indirect command then supplies as its vertex offset.
struct MeshDrawPlacementVK {
    Vector<MeshDrawBindingPlacementVK, RenderAllocator> bindings;
    // First vertex of the draw in the reference binding, binding 0.
    VkDeviceSize first_vertex = 0;
};

struct MeshDrawGroupVK {
    MeshDrawGroupVK() = delete;
    explicit MeshDrawGroupVK(RenderAllocator* p_allocator) : bindings(p_allocator), draw_indices(p_allocator) {
    }
    ~MeshDrawGroupVK() = default;
    MeshDrawGroupVK(const MeshDrawGroupVK&) = delete;
    MeshDrawGroupVK& operator=(const MeshDrawGroupVK&) = delete;
    MeshDrawGroupVK(MeshDrawGroupVK&&) noexcept = default;
    MeshDrawGroupVK& operator=(MeshDrawGroupVK&&) noexcept = default;
    std::span<const RUVertexView> vertex_bindings;
    std::string_view pipeline_variant;
    Vector<MeshDrawBindingPlacementVK, RenderAllocator> bindings;
    VkDeviceSize minimum_first_vertex = 0;
    VkDeviceSize maximum_first_vertex = 0;
    Vector<size_t, RenderAllocator> draw_indices;
};

// Whether two draws read their vertices with the same layout: the same strides and the same attribute roles, formats, and
// offsets in every binding. Only the layout matters, not where the vertices are.
bool mesh_vertex_layouts_equal(std::span<const RUVertexView> p_left, std::span<const RUVertexView> p_right) {
    if (p_left.size() != p_right.size()) {
        return false;
    }
    for (size_t binding_index = 0; binding_index < p_left.size(); ++binding_index) {
        const RUVertexView& left = p_left[binding_index];
        const RUVertexView& right = p_right[binding_index];
        if (left.vertex_stride != right.vertex_stride || left.attribute_count != right.attribute_count) {
            return false;
        }
        for (size_t attribute_index = 0; attribute_index < left.attribute_count; ++attribute_index) {
            const RUVertexView::Attribute& left_attribute = left.attributes.get()[attribute_index];
            const RUVertexView::Attribute& right_attribute = right.attributes.get()[attribute_index];
            if (left_attribute.role.hash != right_attribute.role.hash ||
                left_attribute.role.get_name() != right_attribute.role.get_name() || left_attribute.format != right_attribute.format ||
                left_attribute.offset != right_attribute.offset || left_attribute.size != right_attribute.size) {
                return false;
            }
        }
    }
    return true;
}

MeshDrawPlacementVK make_mesh_draw_placement(RenderAllocator* p_allocator, const MeshDrawBatchPlannerVK::Draw& p_draw) {
    MeshDrawPlacementVK result{.bindings = Vector<MeshDrawBindingPlacementVK, RenderAllocator>(p_allocator)};
    const RUVertexView& reference = p_draw.vertex_bindings.front();
    result.first_vertex = reference.binary.start / reference.vertex_stride;
    result.bindings.reserve(p_draw.vertex_bindings.size());
    for (const RUVertexView& binding : p_draw.vertex_bindings) {
        const VkDeviceSize first_vertex = binding.binary.start / binding.vertex_stride;
        result.bindings.push_back(MeshDrawBindingPlacementVK{
            .binary = binding.binary.binary,
            .vertex_stride = binding.vertex_stride,
            .residue = binding.binary.start % binding.vertex_stride,
            .vertex_delta = static_cast<int64_t>(first_vertex) - static_cast<int64_t>(result.first_vertex),
        });
    }
    return result;
}

bool mesh_group_accepts(const MeshDrawGroupVK& p_group, const MeshDrawBatchPlannerVK::Draw& p_draw, const MeshDrawPlacementVK& p_placement) {
    if (p_group.pipeline_variant != p_draw.pipeline_variant) {
        return false;
    }
    if (!mesh_vertex_layouts_equal(p_group.vertex_bindings, p_draw.vertex_bindings)) {
        return false;
    }
    for (size_t i = 0; i < p_group.bindings.size(); ++i) {
        const MeshDrawBindingPlacementVK& left = p_group.bindings[i];
        const MeshDrawBindingPlacementVK& right = p_placement.bindings[i];
        if (left.binary != right.binary || left.vertex_stride != right.vertex_stride || left.residue != right.residue ||
            left.vertex_delta != right.vertex_delta) {
            return false;
        }
    }
    const VkDeviceSize minimum_first_vertex = std::min(p_group.minimum_first_vertex, p_placement.first_vertex);
    const VkDeviceSize maximum_first_vertex = std::max(p_group.maximum_first_vertex, p_placement.first_vertex);
    return maximum_first_vertex - minimum_first_vertex <= static_cast<VkDeviceSize>(std::numeric_limits<int32_t>::max());
}

void validate_mesh_draw(const MeshDrawBatchPlannerVK::Draw& p_draw) {
    if (p_draw.indices.binary.start % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Failed to plan mesh draws: index buffer offset is not uint32 aligned.");
    }
    if (p_draw.indices.index_count > std::numeric_limits<uint32_t>::max() ||
        p_draw.indices.binary.start / sizeof(uint32_t) > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Failed to plan mesh draws: draw command exceeds Vulkan uint32 limits.");
    }
    if (p_draw.vertex_bindings.empty()) {
        throw std::runtime_error("Failed to plan mesh draws: a draw has no vertex bindings.");
    }
    for (const RUVertexView& binding : p_draw.vertex_bindings) {
        if (binding.vertex_stride == 0 || binding.vertex_stride > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to plan mesh draws: vertex stride must be nonzero and fit Vulkan uint32 limits.");
        }
        if (binding.attribute_count > 0 && binding.attributes == nullptr) {
            throw std::runtime_error("Failed to plan mesh draws: vertex attribute metadata is missing.");
        }
    }
}

} // namespace

MeshDrawBatchPlannerVK::BuildResult::BuildResult(RenderAllocator* p_allocator)
    : commands(p_allocator), batches(p_allocator) {
}

MeshDrawBatchPlannerVK::BuildResult MeshDrawBatchPlannerVK::build(
    RenderAllocator* p_allocator, std::span<const Draw> p_draws, uint32_t p_max_draw_count) const {
    if (p_max_draw_count == 0) {
        throw std::runtime_error("Failed to plan mesh draws: maximum indirect draw count must be nonzero.");
    }
    BuildResult result(p_allocator);
    Vector<MeshDrawGroupVK, RenderAllocator> groups(p_allocator);
    // First vertex of every draw in its reference binding, indexed by draw.
    Vector<VkDeviceSize, RenderAllocator> draw_first_vertices(p_draws.size(), 0, p_allocator);
    size_t drawable_count = 0;
    for (size_t i = 0; i < p_draws.size(); ++i) {
        const Draw& draw = p_draws[i];
        if (draw.indices.index_count == 0 || draw.instance_count == 0) {
            continue;
        }
        validate_mesh_draw(draw);
        if (drawable_count == std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to plan mesh draws: command count exceeds Vulkan uint32 limits.");
        }
        ++drawable_count;
        MeshDrawPlacementVK placement = make_mesh_draw_placement(p_allocator, draw);
        auto group = std::find_if(groups.begin(), groups.end(), [&](const MeshDrawGroupVK& p_group) {
            return mesh_group_accepts(p_group, draw, placement);
        });
        if (group == groups.end()) {
            groups.emplace_back(p_allocator);
            group = groups.end() - 1;
            group->vertex_bindings = draw.vertex_bindings;
            group->pipeline_variant = draw.pipeline_variant;
            group->bindings = std::move(placement.bindings);
            group->minimum_first_vertex = placement.first_vertex;
            group->maximum_first_vertex = placement.first_vertex;
        } else {
            group->minimum_first_vertex = std::min(group->minimum_first_vertex, placement.first_vertex);
            group->maximum_first_vertex = std::max(group->maximum_first_vertex, placement.first_vertex);
        }
        draw_first_vertices[i] = placement.first_vertex;
        group->draw_indices.push_back(i);
    }
    result.commands.reserve(drawable_count);
    for (const MeshDrawGroupVK& group : groups) {
        size_t draws_in_batch = 0;
        for (size_t draw_index : group.draw_indices) {
            if (draws_in_batch == 0 || draws_in_batch == p_max_draw_count) {
                Batch batch;
                batch.first_draw = static_cast<uint32_t>(result.commands.size());
                batch.vertex_bindings = group.vertex_bindings;
                batch.pipeline_variant = group.pipeline_variant;
                batch.vertex_binaries.reserve(group.bindings.size());
                batch.vertex_buffer_offsets.reserve(group.bindings.size());
                for (const MeshDrawBindingPlacementVK& binding : group.bindings) {
                    const auto first_vertex = static_cast<int64_t>(group.minimum_first_vertex) + binding.vertex_delta;
                    batch.vertex_binaries.push_back(binding.binary);
                    batch.vertex_buffer_offsets.push_back(
                        binding.residue + static_cast<VkDeviceSize>(first_vertex) * binding.vertex_stride);
                }
                result.batches.push_back(std::move(batch));
                draws_in_batch = 0;
            }
            const Draw& draw = p_draws[draw_index];
            const VkDeviceSize first_vertex = draw_first_vertices[draw_index];
            result.commands.push_back(VkDrawIndexedIndirectCommand{
                .indexCount = static_cast<uint32_t>(draw.indices.index_count),
                .instanceCount = draw.instance_count,
                .firstIndex = static_cast<uint32_t>(draw.indices.binary.start / sizeof(uint32_t)),
                .vertexOffset = static_cast<int32_t>(first_vertex - group.minimum_first_vertex),
                .firstInstance = draw.first_instance,
            });
            ++result.batches.back().draw_count;
            ++draws_in_batch;
        }
    }
    return result;
}

MeshDrawBatchPlannerVK::BuildResult MeshDrawBatchPlannerVK::build(RenderAllocator* p_allocator,
    std::span<const RUSubmesh3D> p_submeshes,
    std::span<const uint32_t> p_first_instances,
    uint32_t p_max_draw_count) const {
    if (p_submeshes.size() != p_first_instances.size()) {
        throw std::runtime_error("Failed to plan mesh draws: first-instance and submesh counts differ.");
    }
    Vector<Draw, RenderAllocator> draws(p_allocator);
    draws.reserve(p_submeshes.size());
    for (size_t i = 0; i < p_submeshes.size(); ++i) {
        const RUSubmesh3D& submesh = p_submeshes[i];
        if (submesh.instance_count > std::numeric_limits<uint32_t>::max() ||
            submesh.first_instance > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Failed to plan mesh draws: draw command exceeds Vulkan uint32 limits.");
        }
        Draw draw;
        draw.vertex_bindings = submesh.get_vertex_bindings();
        draw.indices = submesh.indices;
        draw.instance_count = static_cast<uint32_t>(submesh.instance_count);
        draw.first_instance = p_first_instances[i];
        if (submesh.material != nullptr) {
            draw.pipeline_variant = submesh.material->get_pipeline_variant();
        }
        draws.push_back(std::move(draw));
    }
    return build(p_allocator, std::span<const Draw>(draws.data(), draws.size()), p_max_draw_count);
}

} // namespace WhiteBirdEngine

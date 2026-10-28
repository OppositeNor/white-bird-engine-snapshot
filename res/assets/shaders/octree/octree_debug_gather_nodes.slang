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
#include "utils/utils.slang"
#include "octree/octree_debug.slang"

/**
 * @brief Reflected descriptor-heap bindings of the debug node gather.
 */
struct OctreeDebugGatherNodesDescriptorIndices {
    /**
     * @brief Descriptor-heap reference to octree_info.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_info", "read")]
    uint2 octree_info;
    /**
     * @brief Descriptor-heap reference to the static octree texture with every mip level.
     */
    [WBE_DESCRIPTOR("sampled_image", "octree_static")]
    uint2 static_levels;
    /**
     * @brief Descriptor-heap reference to the dynamic octree texture with every mip level.
     */
    [WBE_DESCRIPTOR("sampled_image", "octree_dynamic")]
    uint2 dynamic_levels;
    /**
     * @brief Descriptor-heap reference to octree_debug_node_list.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_node_list")]
    uint2 node_list;
    /**
     * @brief Descriptor-heap reference to octree_debug_node_draw_indirect_buffer.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_node_draw_indirect_buffer")]
    uint2 node_indirect_draw;
    /**
     * @brief Renderer frame slot used for shader validation reports.
     */
    [WBE_PUSH_CONSTANT("frame_slot")]
    uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<OctreeDebugGatherNodesDescriptorIndices> descriptor_indices;

/**
 * @brief Appends one node if the static scene or the dynamic meshes occupy it.
 */
void gather_node(OctreeInfo p_info, Texture3D<uint> p_static_levels, Texture3D<uint> p_dynamic_levels, uint p_level, uint3 p_node) {
    if (get_octree_occupancy(p_static_levels, p_dynamic_levels, true, p_level, p_node) == 0) {
        return;
    }
    RWStructuredBuffer<uint> node_list = ResourceDescriptorHeap[descriptor_indices.node_list.x];
    RWStructuredBuffer<DrawIndexedIndirectCommand> node_indirect_draw =
        ResourceDescriptorHeap[descriptor_indices.node_indirect_draw.x];
    uint entry_index;
    InterlockedAdd(node_list[OCTREE_DEBUG_NODE_LIST_COUNT_INDEX], 1, entry_index);
    WBE_SHADER_DEBUG_ASSERT(entry_index < p_info.max_debug_node_count,
        ShaderDebugError::OCTREE_DEBUG_NODE_LIST_OVERFLOW, descriptor_indices.shader_debug_frame_slot);
    if (entry_index >= p_info.max_debug_node_count) {
        return;
    }
    node_list[OCTREE_DEBUG_NODE_LIST_ENTRY_OFFSET + entry_index] = to_octree_debug_node_entry(p_level, p_node);
    InterlockedAdd(node_indirect_draw[0].instance_count, 1);
}

/**
 * @brief Appends every occupied node between the leaves and the root to the node view.
 * @details Each invocation owns one texel of the gather level: it scans every node of its subtree below that level, and the
 * nodes above it whose lowest-corner descendant at the gather level is its texel, so every node is visited exactly once.
 * The dispatch must cover the gather level, which is clamped to max_depth.
 */
[shader("compute")]
[numthreads(4, 4, 4)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<OctreeInfo> octree_info_buffer = ResourceDescriptorHeap[descriptor_indices.octree_info.x];
    Texture3D<uint> static_levels = ResourceDescriptorHeap[descriptor_indices.static_levels.x];
    Texture3D<uint> dynamic_levels = ResourceDescriptorHeap[descriptor_indices.dynamic_levels.x];
    OctreeInfo info = octree_info_buffer[0];
    uint gather_level = min(OCTREE_DEBUG_GATHER_LEVEL, info.max_depth);
    uint3 texel = p_dispatch_thread_id;
    if (any(texel >= (1u << (info.max_depth - gather_level)))) {
        return;
    }
    // The root is drawn as the octree's bounds by the leaf view, so the node view stops below it.
    for (uint level = max(gather_level, 1); level < info.max_depth; ++level) {
        uint span_mask = (1u << (level - gather_level)) - 1;
        if (all((texel & span_mask) == 0)) {
            gather_node(info, static_levels, dynamic_levels, level, texel >> (level - gather_level));
        }
    }
    if (get_octree_occupancy(static_levels, dynamic_levels, true, gather_level, texel) == 0) {
        // Nothing below an empty texel is occupied.
        return;
    }
    for (uint level = 1; level < gather_level; ++level) {
        uint span = 1u << (gather_level - level);
        uint3 first_node = texel * span;
        for (uint z = 0; z < span; ++z) {
            for (uint y = 0; y < span; ++y) {
                for (uint x = 0; x < span; ++x) {
                    gather_node(info, static_levels, dynamic_levels, level, first_node + uint3(x, y, z));
                }
            }
        }
    }
}

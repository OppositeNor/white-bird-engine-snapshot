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
 * @file octree_debug_gather_leaves.slang
 * @brief Builds the leaf view's indirect draws from the leaf level of both octree textures.
 * @details The leaf grid is split into OCTREE_DEBUG_SUBGRID_EXTENT subgrids, and each workgroup gathers one: every
 * invocation tests one OCTREE_DEBUG_SUBGRID_INVOCATION_LEAF_EXTENT block of leaves, and the workgroup writes the subgrid's
 * occupancy mask and one indexed indirect draw command. The
 * command draws one instance per occupied leaf, starting at instance subgrid_index * OCTREE_DEBUG_SUBGRID_LEAF_COUNT, so the
 * vertex shader recovers the subgrid and the leaf's rank in the mask from the instance index. The command after the last
 * subgrid draws the octree's bounds. The dispatch must cover every subgrid with one workgroup each. The pass also empties the
 * node view's list and instance count before the node gather.
 */

/**
 * @brief Reflected descriptor-heap bindings of the debug leaf gather.
 */
struct OctreeDebugGatherLeavesDescriptorIndices {
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
     * @brief Descriptor-heap reference to octree_debug_leaf_masks.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_leaf_masks", "write")]
    uint2 leaf_masks;
    /**
     * @brief Descriptor-heap reference to octree_debug_draw_indirect_buffer.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_draw_indirect_buffer", "write")]
    uint2 indirect_draw;
    /**
     * @brief Descriptor-heap reference to octree_debug_node_list.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_node_list", "write")]
    uint2 node_list;
    /**
     * @brief Descriptor-heap reference to octree_debug_node_draw_indirect_buffer.
     */
    [WBE_DESCRIPTOR("storage_buffer", "octree_debug_node_draw_indirect_buffer", "write")]
    uint2 node_indirect_draw;
};

[[vk::push_constant]]
ConstantBuffer<OctreeDebugGatherLeavesDescriptorIndices> descriptor_indices;

groupshared uint subgrid_mask[OCTREE_DEBUG_SUBGRID_MASK_WORD_COUNT];

[shader("compute")]
// Mirrors OCTREE_DEBUG_SUBGRID_INVOCATION_EXTENT, which numthreads cannot read.
[numthreads(4, 4, 8)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID, uint p_group_index : SV_GroupIndex) {
    StructuredBuffer<OctreeInfo> octree_info_buffer = ResourceDescriptorHeap[descriptor_indices.octree_info.x];
    Texture3D<uint> static_levels = ResourceDescriptorHeap[descriptor_indices.static_levels.x];
    Texture3D<uint> dynamic_levels = ResourceDescriptorHeap[descriptor_indices.dynamic_levels.x];
    RWStructuredBuffer<OctreeDebugSubgridMask> leaf_masks = ResourceDescriptorHeap[descriptor_indices.leaf_masks.x];
    RWStructuredBuffer<DrawIndexedIndirectCommand> indirect_draw = ResourceDescriptorHeap[descriptor_indices.indirect_draw.x];
    OctreeInfo info = octree_info_buffer[0];
    uint3 grid_extent = get_octree_debug_subgrid_grid_extent(info.max_depth);
    uint subgrid_index = get_octree_debug_subgrid_index(p_group_id, grid_extent);

    if (p_group_index < OCTREE_DEBUG_SUBGRID_MASK_WORD_COUNT) {
        subgrid_mask[p_group_index] = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    uint3 first_local_leaf = p_group_thread_id * OCTREE_DEBUG_SUBGRID_INVOCATION_LEAF_EXTENT;
    uint3 first_voxel = p_group_id * OCTREE_DEBUG_SUBGRID_EXTENT + first_local_leaf;
    // The invocation's block is one texel of mip level 1, which is occupied exactly when one of the block's leaves is, so
    // the test skips the leaf loads of empty blocks.
    if (get_octree_occupancy(static_levels, dynamic_levels, true, 1, first_voxel >> 1) != 0) {
        for (uint z = 0; z < OCTREE_DEBUG_SUBGRID_INVOCATION_LEAF_EXTENT.z; ++z) {
            for (uint y = 0; y < OCTREE_DEBUG_SUBGRID_INVOCATION_LEAF_EXTENT.y; ++y) {
                for (uint x = 0; x < OCTREE_DEBUG_SUBGRID_INVOCATION_LEAF_EXTENT.x; ++x) {
                    uint3 offset = uint3(x, y, z);
                    if (get_octree_occupancy(static_levels, dynamic_levels, true, 0, first_voxel + offset) == 0) {
                        continue;
                    }
                    uint bit = get_octree_debug_subgrid_local_index(first_local_leaf + offset);
                    InterlockedOr(subgrid_mask[bit / 32], 1u << (bit % 32));
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (p_group_index != 0) {
        return;
    }
    OctreeDebugSubgridMask mask;
    uint occupied_count = 0;
    for (uint word_index = 0; word_index < OCTREE_DEBUG_SUBGRID_MASK_WORD_COUNT; ++word_index) {
        mask.words[word_index / 4][word_index % 4] = subgrid_mask[word_index];
        occupied_count += countbits(subgrid_mask[word_index]);
    }
    leaf_masks[subgrid_index] = mask;
    DrawIndexedIndirectCommand command;
    command.index_count = OCTREE_DEBUG_CUBE_INDEX_COUNT;
    command.instance_count = occupied_count;
    command.first_index = 0;
    command.vertex_offset = 0;
    command.first_instance = subgrid_index * OCTREE_DEBUG_SUBGRID_LEAF_COUNT;
    indirect_draw[subgrid_index] = command;
    if (subgrid_index != 0) {
        return;
    }
    // The first workgroup also writes the bounds command and empties the node view.
    uint subgrid_count = grid_extent.x * grid_extent.y * grid_extent.z;
    command.instance_count = 1;
    command.first_instance = subgrid_count * OCTREE_DEBUG_SUBGRID_LEAF_COUNT;
    indirect_draw[subgrid_count] = command;
    RWStructuredBuffer<uint> node_list = ResourceDescriptorHeap[descriptor_indices.node_list.x];
    RWStructuredBuffer<DrawIndexedIndirectCommand> node_indirect_draw =
        ResourceDescriptorHeap[descriptor_indices.node_indirect_draw.x];
    node_list[OCTREE_DEBUG_NODE_LIST_COUNT_INDEX] = 0;
    command.instance_count = 0;
    command.first_instance = 0;
    node_indirect_draw[0] = command;
}

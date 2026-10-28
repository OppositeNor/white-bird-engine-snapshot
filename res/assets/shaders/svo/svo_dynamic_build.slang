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
#ifndef WBE_FILE_SVO_DYNAMIC_BUILD_SLANG
#define WBE_FILE_SVO_DYNAMIC_BUILD_SLANG
#include "svo/svo_utils.slang"

/**
 * @brief Extends static topology with dynamic children that are initialized and published by the CAS owner.
 * @details Mirrors the atomic static construction and needs no publication pass.
 * The owner of an EMPTY or LEAF node swaps it to ALLOCATING_DYNAMIC, initializes the dynamic block, and publishes
 * NODE_DYNAMIC or LEAF_DYNAMIC after a device memory barrier; it then continues its own traversal. Contenders that
 * observe ALLOCATING_DYNAMIC or lose the CAS return false so the caller can defer them.
 * @return True when the voxel's leaf occupancy is recorded; false with p_trace at the contended node otherwise.
 */
bool try_dynamic_voxel_allocate_atomic(RWStructuredBuffer<SVONode> p_nodes,
    RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_info,
    inout SVONodeTraceInfo p_trace,
    uint p_frame_slot) {
    // Dynamic nodes follow the static nodes, whose count is fixed while dynamic construction runs.
    uint dynamic_begin = p_info[0].node_count;
    uint node_capacity = get_svo_node_capacity(p_nodes);
    uint leaf_capacity = get_svo_leaf_capacity(p_leaves);
    while (p_trace.remain_depth < 0 || p_trace.remain_depth > 0) {
        uint node_index = p_trace.node_index;
        // Counters grow concurrently within this pass, so only the fixed capacity is a stable bound.
        WBE_SHADER_DEBUG_ASSERT(node_index < node_capacity,
            ShaderDebugError::SVO_NODE_INDEX_OVERFLOW,
            p_frame_slot);
        uint description;
        InterlockedAdd(p_nodes[node_index].children_data, 0, description);
        uint state = description >> SVO_CHILDREN_STATE_SHIFT;
        uint sector = get_svo_node_subregion(p_trace.svo_position);
        if (state == uint(SVONode::ChildrenState::NODE) || state == uint(SVONode::ChildrenState::NODE_DYNAMIC)) {
            // Acquire the children initialized before publication.
            DeviceMemoryBarrier();
            p_trace.node_index = (description & SVO_CHILDREN_BLOCK_INDEX_MASK) * 8 + sector;
            ++p_trace.curr_depth;
            if (p_trace.remain_depth > 0) {
                --p_trace.remain_depth;
            }
            p_trace.svo_position = frac(p_trace.svo_position * 2.0);
            continue;
        }
        if (state == uint(SVONode::ChildrenState::LEAF_DYNAMIC)) {
            uint ignored;
            InterlockedOr(p_nodes[node_index].children_data, 1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + sector), ignored);
            return true;
        }
        if (state == uint(SVONode::ChildrenState::ALLOCATING_DYNAMIC)) {
            return false;
        }
        WBE_SHADER_DEBUG_ASSERT(state == uint(SVONode::ChildrenState::EMPTY) || state == uint(SVONode::ChildrenState::LEAF),
            ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED,
            p_frame_slot);
        uint original;
        uint allocating = uint(SVONode::ChildrenState::ALLOCATING_DYNAMIC) << SVO_CHILDREN_STATE_SHIFT;
        InterlockedCompareExchange(p_nodes[node_index].children_data, description, allocating, original);
        if (original != description) {
            return false;
        }
        p_nodes[node_index].original_children_data = original;
        bool is_leaf = p_trace.curr_depth == p_info[0].max_depth - 1;
        uint children;
        uint published;
        if (is_leaf) {
            uint dynamic_index;
            InterlockedAdd(p_info[0].dynamic_leaves_count, 8, dynamic_index);
            // Dynamic leaves follow the static leaves, whose count is fixed while dynamic construction runs, and share
            // the leaf buffer's capacity with them.
            children = p_info[0].leaves_count + dynamic_index;
            WBE_SHADER_DEBUG_ASSERT(children <= leaf_capacity && leaf_capacity - children >= 8,
                ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW,
                p_frame_slot);
            [unroll] for (uint i = 0; i < 8; ++i) {
                SVOLeaf leaf;
                if (state == uint(SVONode::ChildrenState::LEAF)) {
                    leaf = p_leaves[(original & SVO_CHILDREN_BLOCK_INDEX_MASK) * 8 + i];
                } else {
                    // Dynamic leaf data follows the data of every dynamic node the node buffer can hold.
                    leaf.data_index =
                        p_info[0].data_count + (node_capacity - dynamic_begin) + dynamic_index + i;
                    leaf.parent_index = int(node_index);
                }
                p_leaves[children + i] = leaf;
            }
            // The owner's occupancy is published together with the block.
            uint occupancy = (original & SVO_CHILDREN_OCCUPANCY_MASK) | (1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + sector));
            published = (uint(SVONode::ChildrenState::LEAF_DYNAMIC) << SVO_CHILDREN_STATE_SHIFT) | occupancy | (children / 8);
        } else {
            uint dynamic_index;
            InterlockedAdd(p_info[0].dynamic_node_count, 8, dynamic_index);
            // Dynamic nodes share the node buffer's capacity with the static nodes they follow.
            children = dynamic_begin + dynamic_index;
            WBE_SHADER_DEBUG_ASSERT(children <= node_capacity && node_capacity - children >= 8,
                ShaderDebugError::SVO_NODE_INDEX_OVERFLOW,
                p_frame_slot);
            [unroll] for (uint i = 0; i < 8; ++i) {
                SVONode child;
                child.children_data = uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT;
                child.data_index = p_info[0].data_count + dynamic_index + i;
                child.parent_index = int(node_index);
                child.original_children_data = child.children_data;
                p_nodes[children + i] = child;
            }
            published = (uint(SVONode::ChildrenState::NODE_DYNAMIC) << SVO_CHILDREN_STATE_SHIFT) | (children / 8);
        }
        // Release the initialized block before publishing it.
        DeviceMemoryBarrier();
        uint ignored;
        InterlockedExchange(p_nodes[node_index].children_data, published, ignored);
        if (is_leaf) {
            return true;
        }
        // The owner descends into the block it just published.
        p_trace.node_index = children + sector;
        ++p_trace.curr_depth;
        if (p_trace.remain_depth > 0) {
            --p_trace.remain_depth;
        }
        p_trace.svo_position = frac(p_trace.svo_position * 2.0);
    }
    return true;
}
#endif

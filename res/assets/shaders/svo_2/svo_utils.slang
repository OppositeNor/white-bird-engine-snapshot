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
#ifndef WBE_FILE_SVO_2_UTILS_SLANG
#define WBE_FILE_SVO_2_UTILS_SLANG

#include "svo/svo_common.slang"

/**
 * @brief Reads a description whose state and block index remain stable throughout a construction pass.
 * @details Only EMPTY changes within a pass, to ALLOCATING; leaf occupancy bits may also grow. Newly initialized children
 * become reachable only after the state-update dispatch and its memory dependency.
 */
SVONode load_svo_node(RWStructuredBuffer<SVONode> p_nodes, uint p_node_index) {
    return p_nodes[p_node_index];
}

/**
 * @brief Marks occupancy on a published leaf without changing its state or block index.
 */
void occupy_svo_leaf(RWStructuredBuffer<SVONode> p_nodes, uint p_node_index, float3 p_position) {
    uint original_data;
    InterlockedOr(p_nodes[p_node_index].children_data,
        1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + get_svo_node_subregion(p_position)),
        original_data);
}

/**
 * @brief Reserves an eight-node block or reports capacity exhaustion to shader validation.
 * @details Uses one atomic add. Capacity and packed-index bounds are checked only when shader debugging is enabled.
 */
bool try_reserve_svo_nodes(RWStructuredBuffer<SVONode> p_nodes, RWStructuredBuffer<SVOInfo> p_tree_info, uint p_frame_slot,
    out uint p_children_index) {
    uint current_count;
    InterlockedAdd(p_tree_info[0].node_count, 8, current_count);
    WBE_SHADER_DEBUG_ASSERT(current_count / 8 <= SVO_CHILDREN_BLOCK_INDEX_MASK &&
        current_count <= get_svo_node_capacity(p_nodes) &&
        get_svo_node_capacity(p_nodes) - current_count >= 8,
        ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
    p_children_index = current_count;
    return true;
}

/**
 * @brief Initializes an unreachable eight-node block.
 */
bool create_svo_children_node(RWStructuredBuffer<SVONode> p_nodes,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    uint p_parent_node_index,
    uint p_frame_slot,
    out uint p_children_index) {
    if (!try_reserve_svo_nodes(p_nodes, p_tree_info, p_frame_slot, p_children_index)) {
        return false;
    }
    [unroll] for (uint i = 0; i < 8; ++i) {
        // Initialize all of the children to empty
        uint child_index = p_children_index + i;
        p_nodes[child_index].children_data = uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT;
        p_nodes[child_index].parent_index = p_parent_node_index;
    }
    uint new_data_index;
    InterlockedAdd(p_tree_info[0].data_count, 8, new_data_index);
    [unroll] for (uint i = 0; i < 8; ++i) {
        p_nodes[p_children_index + i].data_index = new_data_index + i;
    }
    return true;
}

/**
 * @brief Reserves an eight-leaf block or reports capacity exhaustion to shader validation.
 * @details Uses one atomic add. Capacity and packed-index bounds are checked only when shader debugging is enabled.
 */
bool try_reserve_svo_leaves(RWStructuredBuffer<SVOLeaf> p_leaves, RWStructuredBuffer<SVOInfo> p_tree_info, uint p_frame_slot,
    out uint p_children_index) {
    uint current_count;
    InterlockedAdd(p_tree_info[0].leaves_count, 8, current_count);
    WBE_SHADER_DEBUG_ASSERT(current_count / 8 <= SVO_CHILDREN_BLOCK_INDEX_MASK &&
        current_count <= get_svo_leaf_capacity(p_leaves) &&
        get_svo_leaf_capacity(p_leaves) - current_count >= 8,
        ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW, p_frame_slot);
    p_children_index = current_count;
    return true;
}

/**
 * @brief Initializes an unreachable eight-leaf block before inter-pass publication.
 */
bool create_svo_children_leaf(RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    uint p_parent_node_index,
    uint p_frame_slot,
    out uint p_children_index) {
    if (!try_reserve_svo_leaves(p_leaves, p_tree_info, p_frame_slot, p_children_index)) {
        return false;
    }
    [unroll] for (uint i = 0; i < 8; ++i) {
        uint child_index = p_children_index + i;
        p_leaves[child_index].parent_index = p_parent_node_index;
    }
    uint new_data_index;
    InterlockedAdd(p_tree_info[0].data_count, 8, new_data_index);
    [unroll] for (uint i = 0; i < 8; ++i) {
        p_leaves[p_children_index + i].data_index = new_data_index + i;
    }
    return true;
}

/**
 * @brief Claims an empty node and prepares children for the next state-update pass.
 * @details Only the CAS winner initializes children. Traversal treats ALLOCATING as a retry and never inspects its
 * payload. A leaf payload includes the creator's occupancy bit so zero occupancy unambiguously means a node block.
 */
bool try_create_svo_entry(RWStructuredBuffer<SVONode> p_nodes, RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    uint p_node_index, bool p_is_leaf, float3 p_position, uint p_frame_slot) {
    uint original;
    InterlockedCompareExchange(p_nodes[p_node_index].children_data,
        uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT,
        uint(SVONode::ChildrenState::ALLOCATING) << SVO_CHILDREN_STATE_SHIFT, original);
    if (original != (uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT)) {
        return false;
    }
    uint children_index;
    bool allocated;
    if (p_is_leaf) {
        allocated = create_svo_children_leaf(p_leaves, p_tree_info, p_node_index, p_frame_slot, children_index);
    }
    else {
        allocated = create_svo_children_node(p_nodes, p_tree_info, p_node_index, p_frame_slot, children_index);
    }
    if (!allocated) {
        InterlockedExchange(
            p_nodes[p_node_index].children_data, uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT, original);
        return false;
    }
    uint occupancy = p_is_leaf ? (1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + get_svo_node_subregion(p_position))) : 0;
    uint pending_data =
        (uint(SVONode::ChildrenState::ALLOCATING) << SVO_CHILDREN_STATE_SHIFT) | occupancy | (children_index / 8);
    // Other invocations may still CAS this word. Publication and visibility are provided by the later dispatch boundary.
    InterlockedExchange(p_nodes[p_node_index].children_data, pending_data, original);
    return true;
}

/**
 * @brief Marks a published leaf occupied or saves traversal for the next construction round.
 */
bool try_voxel_allocate(RWStructuredBuffer<SVONode> p_nodes, RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    inout SVONodeTraceInfo p_trace_info, uint p_frame_slot) {
    uint node_index = p_trace_info.node_index;
    uint curr_depth = p_trace_info.curr_depth;
    WBE_SHADER_DEBUG_ASSERT(p_tree_info[0].max_depth >= 2 &&
        get_svo_node_capacity(p_nodes) >= 8 &&
        get_svo_leaf_capacity(p_leaves) != 0,
        ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
    for (int depth = 0; p_trace_info.remain_depth < 0 || depth < p_trace_info.remain_depth; ++depth) {
        WBE_SHADER_DEBUG_ASSERT(curr_depth < p_tree_info[0].max_depth,
            ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
        WBE_SHADER_DEBUG_ASSERT(node_index < get_svo_node_capacity(p_nodes),
            ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
        SVONode node = load_svo_node(p_nodes, node_index);
        if (get_svo_children_state(node) == SVONode::ChildrenState::EMPTY) {
            try_create_svo_entry(p_nodes, p_leaves, p_tree_info, node_index,
                curr_depth == p_tree_info[0].max_depth - 1, p_trace_info.svo_position, p_frame_slot);
            // Even the owner retries from this node after publication; no invocation descends into new children this pass.
            p_trace_info.node_index = node_index;
            p_trace_info.curr_depth = curr_depth;
            p_trace_info.remain_depth = p_trace_info.remain_depth < 0 ? p_trace_info.remain_depth : p_trace_info.remain_depth - depth;
            return false;
        }
        SVONode::ChildrenState child_state = get_svo_children_state(node);
        switch (child_state) {
        case SVONode::ChildrenState::NODE:
            // A node, keep on looking deeper.
            WBE_SHADER_DEBUG_ASSERT(curr_depth < p_tree_info[0].max_depth - 1,
                ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
            WBE_SHADER_DEBUG_ASSERT(get_svo_children_index(node) <= get_svo_node_capacity(p_nodes) &&
                get_svo_node_capacity(p_nodes) - get_svo_children_index(node) >= 8,
                ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
            break;
        case SVONode::ChildrenState::LEAF:
            // Found a leaf, allocation success.
            WBE_SHADER_DEBUG_ASSERT(curr_depth >= p_tree_info[0].max_depth - 1,
                ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
            WBE_SHADER_DEBUG_ASSERT(curr_depth <= p_tree_info[0].max_depth - 1,
                ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
            WBE_SHADER_DEBUG_ASSERT(get_svo_children_index(node) <= get_svo_leaf_capacity(p_leaves) &&
                get_svo_leaf_capacity(p_leaves) - get_svo_children_index(node) >= 8,
                ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW, p_frame_slot);
            occupy_svo_leaf(p_nodes, node_index, p_trace_info.svo_position);
            return true;
        case SVONode::ChildrenState::ALLOCATING:
            // Traced to an empty/allocating/leaf node, set p_state and return.
            p_trace_info.node_index = node_index;
            p_trace_info.curr_depth = curr_depth;
            p_trace_info.remain_depth = p_trace_info.remain_depth < 0 ? p_trace_info.remain_depth : p_trace_info.remain_depth - depth;
            return false;
        default:
            WBE_SHADER_DEBUG_ASSERT(false, curr_depth < p_tree_info[0].max_depth - 1
                    ? ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED
                    : ShaderDebugError::SVO_DEEPER_THAN_EXPECTED,
                p_frame_slot);
            break;
        }
        uint child_index = get_svo_children_index(node) + get_svo_node_subregion(p_trace_info.svo_position);
        node_index = child_index;
        ++curr_depth;
        // Remap from parent-local coordinates into child-local coordinates.
        p_trace_info.svo_position = frac(p_trace_info.svo_position * 2.0);
    }
    // We've traced a node.
    p_trace_info.node_index = node_index;
    p_trace_info.curr_depth = curr_depth;
    p_trace_info.remain_depth = 0;
    return true;
}

#endif

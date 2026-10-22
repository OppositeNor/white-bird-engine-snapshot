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
#ifndef WBE_FILE_SVO_UTILS_SLANG
#define WBE_FILE_SVO_UTILS_SLANG

#include "svo/svo_common.slang"

/**
 * @brief Loads an SVO node after acquiring its published child description.
 * @details The barrier makes initialization preceding NODE or LEAF publication visible to the reader.
 */
SVONode load_svo_node(RWStructuredBuffer<SVONode> p_nodes, uint p_node_index) {
    uint children_data;
    InterlockedAdd(p_nodes[p_node_index].children_data, 0, children_data);
    SVONode::ChildrenState state = SVONode::ChildrenState(children_data >> SVO_CHILDREN_STATE_SHIFT);
    if (state == SVONode::ChildrenState::NODE || state == SVONode::ChildrenState::LEAF) {
        DeviceMemoryBarrier();
    }
    SVONode result;
    result.children_data = children_data;
    result.data_index = p_nodes[p_node_index].data_index;
    result.parent_index = p_nodes[p_node_index].parent_index;
    return result;
}

/**
 * @brief Publishes initialized children and their block index as one atomic description.
 * @details The barrier makes initialization of all eight children visible before NODE or LEAF publication.
 */
void publish_svo_children_data(RWStructuredBuffer<SVONode> p_nodes, uint p_node_index, uint p_children_data) {
    DeviceMemoryBarrier();
    uint original_data;
    InterlockedExchange(p_nodes[p_node_index].children_data, p_children_data, original_data);
}

/**
 * @brief Marks just the intersected leaf occupied, preserving concurrent insertions into the same block.
 */
void occupy_svo_leaf(RWStructuredBuffer<SVONode> p_nodes, uint p_node_index, float3 p_position) {
    uint original_data;
    InterlockedOr(p_nodes[p_node_index].children_data,
        1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + get_svo_node_subregion(p_position)),
        original_data);
}

bool try_reserve_svo_nodes(RWStructuredBuffer<SVOInfo> p_tree_info, uint p_frame_slot, out uint p_children_index) {
    InterlockedAdd(p_tree_info[0].node_count, 8, p_children_index);
    WBE_SHADER_DEBUG_ASSERT(p_children_index / 8 <= SVO_CHILDREN_BLOCK_INDEX_MASK &&
        p_children_index <= p_tree_info[0].max_node_count &&
        p_tree_info[0].max_node_count - p_children_index >= 8,
        ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
    return true;
}

bool create_svo_children_node(RWStructuredBuffer<SVONode> p_nodes,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    uint p_parent_node_index,
    uint p_frame_slot,
    out uint p_children_index) {
    if (!try_reserve_svo_nodes(p_tree_info, p_frame_slot, p_children_index)) {
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

bool try_reserve_svo_leaves(RWStructuredBuffer<SVOInfo> p_tree_info, uint p_frame_slot, out uint p_children_index) {
    InterlockedAdd(p_tree_info[0].leaves_count, 8, p_children_index);
    WBE_SHADER_DEBUG_ASSERT(p_children_index / 8 <= SVO_CHILDREN_BLOCK_INDEX_MASK &&
        p_children_index <= p_tree_info[0].max_leaves_count &&
        p_tree_info[0].max_leaves_count - p_children_index >= 8,
        ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW, p_frame_slot);
    return true;
}

bool create_svo_children_leaf(RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    uint p_parent_node_index,
    uint p_frame_slot,
    out uint p_children_index) {
    if (!try_reserve_svo_leaves(p_tree_info, p_frame_slot, p_children_index)) {
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

bool try_create_svo_entry(RWStructuredBuffer<SVONode> p_nodes, RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_tree_info,
    uint p_node_index, bool p_is_leaf, uint p_frame_slot) {
    WBE_SHADER_DEBUG_ASSERT(p_node_index < p_tree_info[0].max_node_count,
        ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
    uint original;
    // Try swaping to ALLOCATE state
    InterlockedCompareExchange(
        p_nodes[p_node_index].children_data,
        uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT,
        uint(SVONode::ChildrenState::ALLOCATING) << SVO_CHILDREN_STATE_SHIFT,
        original
    );
    if (original != (uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT)) {
        // CAS Failed. Allocation failed.
        return false;
    }
    // CAS succeeded. Allocate the child node:
    uint children_index;
    if (p_is_leaf) {
        if (!create_svo_children_leaf(p_leaves, p_tree_info, p_node_index, p_frame_slot, children_index)) {
            publish_svo_children_data(
                p_nodes, p_node_index, uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT);
            return false;
        }
        publish_svo_children_data(
            p_nodes,
            p_node_index,
            (uint(SVONode::ChildrenState::LEAF) << SVO_CHILDREN_STATE_SHIFT) | (children_index / 8));
    }
    else {
        if (!create_svo_children_node(p_nodes, p_tree_info, p_node_index, p_frame_slot, children_index)) {
            publish_svo_children_data(
                p_nodes, p_node_index, uint(SVONode::ChildrenState::EMPTY) << SVO_CHILDREN_STATE_SHIFT);
            return false;
        }
        publish_svo_children_data(p_nodes, p_node_index, children_index / 8);
    }
    return true;
}

bool try_voxel_allocate(RWStructuredBuffer<SVONode> p_nodes, RWStructuredBuffer<SVOLeaf> p_leaves,
    RWStructuredBuffer<SVOInfo> p_tree_info, inout SVONodeTraceInfo p_trace_info, uint p_frame_slot) {
    uint node_index = p_trace_info.node_index;
    uint curr_depth = p_trace_info.curr_depth;
    WBE_SHADER_DEBUG_ASSERT(p_tree_info[0].max_depth >= 2 &&
        p_tree_info[0].max_node_count >= 8 &&
        p_tree_info[0].max_leaves_count != 0,
        ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
    for (int depth = 0; p_trace_info.remain_depth < 0 || depth < p_trace_info.remain_depth; ++depth) {
        WBE_SHADER_DEBUG_ASSERT(curr_depth < p_tree_info[0].max_depth,
            ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
        WBE_SHADER_DEBUG_ASSERT(node_index < p_tree_info[0].max_node_count,
            ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
        SVONode node = load_svo_node(p_nodes, node_index);
        if (get_svo_children_state(node) == SVONode::ChildrenState::EMPTY) {
            if (!try_create_svo_entry(
                    p_nodes, p_leaves, p_tree_info, node_index, curr_depth == p_tree_info[0].max_depth - 1, p_frame_slot)) {
                p_trace_info.node_index = node_index;
                p_trace_info.curr_depth = curr_depth;
                p_trace_info.remain_depth = p_trace_info.remain_depth < 0 ? p_trace_info.remain_depth : p_trace_info.remain_depth - depth;
                return false;
            }
            node = load_svo_node(p_nodes, node_index);
        }
        SVONode::ChildrenState child_state = get_svo_children_state(node);
        switch (child_state) {
        case SVONode::ChildrenState::NODE:
            // A node, keep on looking deeper.
            WBE_SHADER_DEBUG_ASSERT(curr_depth < p_tree_info[0].max_depth - 1,
                ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
            WBE_SHADER_DEBUG_ASSERT(get_svo_children_index(node) <= p_tree_info[0].max_node_count &&
                p_tree_info[0].max_node_count - get_svo_children_index(node) >= 8,
                ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
            break;
        case SVONode::ChildrenState::LEAF:
            // Found a leaf, allocation success.
            WBE_SHADER_DEBUG_ASSERT(curr_depth >= p_tree_info[0].max_depth - 1,
                ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
            WBE_SHADER_DEBUG_ASSERT(curr_depth <= p_tree_info[0].max_depth - 1,
                ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
            WBE_SHADER_DEBUG_ASSERT(get_svo_children_index(node) <= p_tree_info[0].max_leaves_count &&
                p_tree_info[0].max_leaves_count - get_svo_children_index(node) >= 8,
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

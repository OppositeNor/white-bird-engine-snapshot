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
#ifndef WBE_FILE_SVO_COMMON_SLANG
#define WBE_FILE_SVO_COMMON_SLANG

#include "svo/svo_deferred_list.slang"

/**
 * @brief Four scalar fields with a 16-byte storage-buffer array stride.
 */
[WBE_DATA_TYPE]
struct SVONode {
    enum class ChildrenState {
        NODE = 0,
        EMPTY = 1,
        ALLOCATING = 2,
        LEAF = 3,
        ALLOCATING_DYNAMIC = 4,
        NODE_DYNAMIC = 5,
        LEAF_DYNAMIC = 6
    };
    /**
     * @brief Published child description.
     * @details Bits 31:29 encode NODE=0, EMPTY=1, ALLOCATING=2, LEAF=3,
     * ALLOCATING_DYNAMIC=4, NODE_DYNAMIC=5 and LEAF_DYNAMIC=6.
     * LEAF uses bits 28:21 for occupancy (bit 21 is sector zero) and bits 20:0 for an eight-leaf block index.
     * NODE uses bits 20:0 for an eight-node block index; bits 28:21 are unused.
     * NODE_DYNAMIC stores a virtual block index offset by max_node_count; LEAF_DYNAMIC indexes a dynamic leaf block.
     * EMPTY, ALLOCATING and ALLOCATING_DYNAMIC ignore the lower twenty-nine bits during traversal. In svo_2,
     * ALLOCATING and ALLOCATING_DYNAMIC carry the new block index and occupancy until the publication pass, where zero
     * occupancy identifies a node block and nonzero occupancy identifies a leaf block.
     */
    uint children_data;
    /**
     * @brief Index of the data in the data buffer.
     */
    uint data_index;
    /**
     * @brief The index to the parent node. < 0 if no parent present, which represents
     * this node as one of the implicit root's eight sectors.
     */
    int parent_index;
    /**
     * @brief Original child description saved by the dynamic allocation owner.
     */
    uint original_children_data;
};

/**
 * @brief Bit offset of the ChildrenState field (bits 31:29) in SVONode::children_data.
 */
static const uint SVO_CHILDREN_STATE_SHIFT = 29;
/**
 * @brief Bit offset of sector zero's occupancy bit in SVONode::children_data.
 */
static const uint SVO_CHILDREN_OCCUPANCY_SHIFT = 21;
/**
 * @brief Mask of the eight sector occupancy bits (bits 28:21) in SVONode::children_data.
 */
static const uint SVO_CHILDREN_OCCUPANCY_MASK = 0x1fe00000u;
/**
 * @brief Mask of the eight-child block index (bits 20:0) in SVONode::children_data.
 */
static const uint SVO_CHILDREN_BLOCK_INDEX_MASK = 0x001fffffu;
/**
 * @brief Mask of every bit below the ChildrenState field (occupancy and block index) in SVONode::children_data.
 */
static const uint SVO_CHILDREN_PAYLOAD_MASK = 0x1fffffffu;

/**
 * @brief One initialized leaf slot; occupancy is published in its parent node.
 */
[WBE_DATA_TYPE]
struct SVOLeaf {
    /**
     * @brief Index of the data in the data buffer.
     */
    uint data_index;
    /**
     * @brief Index of the stored node owning this eight-leaf block; always nonnegative.
     */
    int parent_index;
};

/**
 * @brief Immutable spatial configuration and mutable construction counters.
 */
[WBE_DATA_TYPE]
struct SVOInfo {
    /**
     * @brief The origin of the SVO in world space.
     */
    float3 origin;
    /**
     * @brief The width of the region this SVO is covering.
     */
    float width;
    /**
     * @brief The maximum depth of the SVO, at least two; stored nodes start at depth one.
     */
    uint max_depth;
    /**
     * @brief The capacity of the node buffer.
     */
    uint max_node_count;
    /**
     * @brief The capacity of the leaf buffer.
     */
    uint max_leaves_count;
    /**
     * @brief The number of stored nodes, including the first eight sectors but excluding the implicit root.
     */
    uint node_count;
    /**
     * @brief The number of leaves.
     */
    uint leaves_count;
    /**
     * @brief The number of data blocks.
     */
    uint data_count;
    /**
     * @brief The maximum number of entries each deferred list can hold, excluding its count element.
     */
    uint max_deferred_task_count;
    /**
     * @brief Capacity of the separate dynamic node array.
     */
    uint max_dynamic_node_count;
    /**
     * @brief Capacity in leaf slots; the dynamic array stores blocks of eight leaves plus a saved parent word.
     */
    uint max_dynamic_leaves_count;
    /**
     * @brief Allocated dynamic node slots, reset after restoring static parent links.
     */
    uint dynamic_node_count;
    /**
     * @brief Allocated dynamic leaf slots, always a multiple of eight.
     */
    uint dynamic_leaves_count;
};

/**
 * @brief Selects one of eight sectors in parent-local coordinates.
 */
uint get_svo_node_subregion(float3 p_position)
{
    return (uint(p_position.x >= 0.5) << 2) |
        (uint(p_position.y >= 0.5) << 1) |
        uint(p_position.z >= 0.5);
}

/**
 * @brief Decodes the state from a snapshot of a node's child description.
 */
SVONode::ChildrenState get_svo_children_state(SVONode p_node) {
    return SVONode::ChildrenState(p_node.children_data >> SVO_CHILDREN_STATE_SHIFT);
}

/**
 * @brief Decodes the first child index for a NODE or LEAF description.
 */
uint get_svo_children_index(SVONode p_node) {
    return (p_node.children_data & SVO_CHILDREN_BLOCK_INDEX_MASK) * 8;
}

/**
 * @brief Tests an allocated leaf slot's occupancy for debug drawing after construction.
 * @details Invalid parent links report a shader validation error.
 */
bool is_svo_leaf_occupied(uint p_leaf_index,
    StructuredBuffer<SVONode> p_nodes,
    StructuredBuffer<SVOLeaf> p_leaves,
    SVOInfo p_tree_info,
    uint p_frame_slot) {
    int parent_index = p_leaves[p_leaf_index].parent_index;
    WBE_SHADER_DEBUG_ASSERT(parent_index >= 0 && uint(parent_index) < p_tree_info.node_count,
        ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
    return (p_nodes[parent_index].children_data & (1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + p_leaf_index % 8))) != 0;
}

/**
 * @brief Traverses only static NODE children, stopping at the first node in any other state.
 * @details p_trace_info.node_index is a static node index; p_trace_info.index_type is ignored. Node descriptions are
 * read without synchronization, so they must not change during the calling pass.
 */
void traverse_static_svo(RWStructuredBuffer<SVONode> p_nodes, inout SVONodeTraceInfo p_trace_info) {
    uint node_index = p_trace_info.node_index;
    uint curr_depth = p_trace_info.curr_depth;
    for (int depth = 0; p_trace_info.remain_depth < 0 || depth < p_trace_info.remain_depth; ++depth) {
        SVONode node = p_nodes[node_index];
        SVONode::ChildrenState children_state = get_svo_children_state(node);
        if (children_state != SVONode::ChildrenState::NODE) {
            // Traced to an empty/allocating/leaf node, set p_state and return.
            p_trace_info.node_index = node_index;
            p_trace_info.curr_depth = curr_depth;
            p_trace_info.remain_depth = p_trace_info.remain_depth < 0 ? p_trace_info.remain_depth : p_trace_info.remain_depth - depth;
            return;
        }
        node_index = get_svo_children_index(node) + get_svo_node_subregion(p_trace_info.svo_position);
        ++curr_depth;
        // Remap from parent-local coordinates into child-local coordinates.
        p_trace_info.svo_position = frac(p_trace_info.svo_position * 2.0);
    }
    // We've traced a node.
    p_trace_info.curr_depth = curr_depth;
    p_trace_info.node_index = node_index;
    p_trace_info.remain_depth = 0;
}

/**
 * @brief Descends from p_trace_info.node_index toward p_trace_info.svo_position without allocating.
 * @details p_trace_info.node_index indexes the storage selected by p_trace_info.index_type on input and output.
 * Traversal follows NODE children and descends into the voxel's leaf when its parent is LEAF and the leaf is occupied.
 * With p_traverse_dynamic, NODE_DYNAMIC and LEAF_DYNAMIC children are followed the same way; otherwise traversal stops at
 * them. It stops at EMPTY, ALLOCATING, ALLOCATING_DYNAMIC, unoccupied leaves, leaves, or when remain_depth steps are
 * exhausted, leaving p_trace_info at the last reached node or leaf with remain_depth reduced by the steps taken. Node
 * descriptions are read without synchronization, so they must not change during the calling pass.
 * @param p_nodes Static SVO node storage.
 * @param p_dynamic_nodes Dynamic SVO node storage; read only when p_traverse_dynamic is true.
 * @param p_tree_info Tree info supplying the virtual offset of dynamic node blocks.
 * @param p_traverse_dynamic Whether to follow dynamic children.
 * @param p_trace_info Traversal state, updated in place.
 */
void traverse_svo(RWStructuredBuffer<SVONode> p_nodes,
    RWStructuredBuffer<SVONode> p_dynamic_nodes,
    SVOInfo p_tree_info,
    bool p_traverse_dynamic,
    inout SVONodeTraceInfo p_trace_info) {
    uint node_index = p_trace_info.node_index;
    SVONodeTraceInfo::IndexType index_type = p_trace_info.index_type;
    uint curr_depth = p_trace_info.curr_depth;
    int depth = 0;
    for (; p_trace_info.remain_depth < 0 || depth < p_trace_info.remain_depth; ++depth) {
        if (index_type == SVONodeTraceInfo::IndexType::STATIC_LEAF ||
            index_type == SVONodeTraceInfo::IndexType::DYNAMIC_LEAF) {
            // Leaves have no children.
            break;
        }
        SVONode node;
        if (index_type == SVONodeTraceInfo::IndexType::STATIC_NODE) {
            node = p_nodes[node_index];
        } else {
            node = p_dynamic_nodes[node_index];
        }
        uint sector = get_svo_node_subregion(p_trace_info.svo_position);
        bool is_occupied = (node.children_data & (1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + sector))) != 0;
        uint children_index = get_svo_children_index(node);
        SVONode::ChildrenState children_state = get_svo_children_state(node);
        if (children_state == SVONode::ChildrenState::NODE) {
            index_type = SVONodeTraceInfo::IndexType::STATIC_NODE;
            node_index = children_index + sector;
        } else if (children_state == SVONode::ChildrenState::NODE_DYNAMIC && p_traverse_dynamic) {
            // NODE_DYNAMIC block indices are virtual, offset by the static node capacity.
            index_type = SVONodeTraceInfo::IndexType::DYNAMIC_NODE;
            node_index = children_index - p_tree_info.max_node_count + sector;
        } else if (children_state == SVONode::ChildrenState::LEAF && is_occupied) {
            index_type = SVONodeTraceInfo::IndexType::STATIC_LEAF;
            node_index = children_index + sector;
        } else if (children_state == SVONode::ChildrenState::LEAF_DYNAMIC && p_traverse_dynamic && is_occupied) {
            index_type = SVONodeTraceInfo::IndexType::DYNAMIC_LEAF;
            node_index = children_index + sector;
        } else {
            // Traced to an empty/allocating node, an unoccupied leaf, or a dynamic child not being followed.
            break;
        }
        ++curr_depth;
        // Remap from parent-local coordinates into child-local coordinates.
        p_trace_info.svo_position = frac(p_trace_info.svo_position * 2.0);
    }
    p_trace_info.node_index = node_index;
    p_trace_info.index_type = index_type;
    p_trace_info.curr_depth = curr_depth;
    p_trace_info.remain_depth = p_trace_info.remain_depth < 0 ? p_trace_info.remain_depth : p_trace_info.remain_depth - depth;
}

#endif

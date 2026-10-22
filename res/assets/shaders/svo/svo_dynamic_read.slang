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
#ifndef WBE_FILE_SVO_DYNAMIC_READ_SLANG
#define WBE_FILE_SVO_DYNAMIC_READ_SLANG
#include "svo/svo_dynamic_types.slang"
/**
 * @brief Resolves a virtual node index in static or dynamic storage.
 */
SVONode read_incremental_node(
    StructuredBuffer<SVONode> p_nodes, StructuredBuffer<SVONode> p_dynamic_nodes, SVOInfo p_info, uint p_index) {
    if (p_index < p_info.max_node_count) {
        return p_nodes[p_index];
    }
    return p_dynamic_nodes[p_index - p_info.max_node_count];
}
/**
 * @brief Resolves a debug instance into static leaves followed by dynamic leaves.
 */
SVOLeaf read_incremental_leaf(
    StructuredBuffer<SVOLeaf> p_leaves, StructuredBuffer<SVODynamicLeafBlock> p_dynamic_leaves, SVOInfo p_info, uint p_index) {
    if (p_index < p_info.leaves_count) {
        return p_leaves[p_index];
    }
    uint local = p_index - p_info.leaves_count;
    return p_dynamic_leaves[local / 8].leaves[local % 8];
}
/**
 * @brief Hides superseded static leaf blocks and tests occupancy of the currently referenced block.
 */
bool is_incremental_leaf_occupied(uint p_index,
    StructuredBuffer<SVONode> p_nodes,
    StructuredBuffer<SVOLeaf> p_leaves,
    StructuredBuffer<SVONode> p_dynamic_nodes,
    StructuredBuffer<SVODynamicLeafBlock> p_dynamic_leaves,
    SVOInfo p_info,
    uint p_frame_slot) {
    SVOLeaf leaf = read_incremental_leaf(p_leaves, p_dynamic_leaves, p_info, p_index);
    WBE_SHADER_DEBUG_ASSERT(leaf.parent_index >= 0 && uint(leaf.parent_index) < p_info.max_node_count + p_info.dynamic_node_count,
        ShaderDebugError::SVO_NODE_INDEX_OVERFLOW,
        p_frame_slot);
    SVONode parent = read_incremental_node(p_nodes, p_dynamic_nodes, p_info, uint(leaf.parent_index));
    bool dynamic = p_index >= p_info.leaves_count;
    uint local = dynamic ? p_index - p_info.leaves_count : p_index;
    uint expected = uint(dynamic ? SVONode::ChildrenState::LEAF_DYNAMIC : SVONode::ChildrenState::LEAF);
    if ((parent.children_data >> SVO_CHILDREN_STATE_SHIFT) != expected ||
        get_svo_children_index(parent) / 8 != local / 8) {
        return false;
    }
    return (parent.children_data & (1u << (SVO_CHILDREN_OCCUPANCY_SHIFT + local % 8))) != 0;
}
/**
 * @brief Reconstructs one leaf's grid position by walking from the leaf to the root.
 * @param p_leaf_index Virtual leaf index covering static leaves followed by dynamic leaves.
 * @param p_nodes Static SVO node storage.
 * @param p_dynamic_nodes Dynamic SVO node storage following the static nodes.
 * @param p_leaves Static SVO leaf storage.
 * @param p_dynamic_leaves Dynamic SVO leaf blocks following the static leaves.
 * @param p_tree_info Tree bounds and maximum depth.
 * @param p_frame_slot Renderer frame slot whose report region receives validation errors.
 * @return Integer position of the leaf in the finest SVO grid.
 */
uint3 get_leaf_grid_position(uint p_leaf_index,
    StructuredBuffer<SVONode> p_nodes,
    StructuredBuffer<SVONode> p_dynamic_nodes,
    StructuredBuffer<SVOLeaf> p_leaves,
    StructuredBuffer<SVODynamicLeafBlock> p_dynamic_leaves,
    SVOInfo p_tree_info,
    uint p_frame_slot) {
    uint3 result = uint3(0);
    WBE_SHADER_DEBUG_ASSERT(p_leaf_index < p_tree_info.leaves_count + p_tree_info.dynamic_leaves_count,
        ShaderDebugError::SVO_LEAF_INDEX_OVERFLOW, p_frame_slot);
    uint child_index = p_leaf_index < p_tree_info.leaves_count ? p_leaf_index : p_leaf_index - p_tree_info.leaves_count;
    int parent_index = read_incremental_leaf(p_leaves, p_dynamic_leaves, p_tree_info, p_leaf_index).parent_index;
    uint traversed_depth = 0;
    for (; traversed_depth < p_tree_info.max_depth && parent_index >= 0; ++traversed_depth) {
        WBE_SHADER_DEBUG_ASSERT(uint(parent_index) < p_tree_info.max_node_count + p_tree_info.dynamic_node_count,
            ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
        SVONode parent = read_incremental_node(p_nodes, p_dynamic_nodes, p_tree_info, uint(parent_index));
        WBE_SHADER_DEBUG_ASSERT(child_index >= get_svo_children_index(parent) &&
            child_index - get_svo_children_index(parent) < 8,
            ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
        uint subregion = child_index % 8;
        result.x |= ((subregion >> 2) & 1) << traversed_depth;
        result.y |= ((subregion >> 1) & 1) << traversed_depth;
        result.z |= (subregion & 1) << traversed_depth;
        child_index = uint(parent_index);
        parent_index = parent.parent_index;
    }
    if (parent_index < 0) {
        WBE_SHADER_DEBUG_ASSERT(child_index < 8 && traversed_depth < 32,
            ShaderDebugError::SVO_NODE_INDEX_OVERFLOW, p_frame_slot);
        // The remaining node is one of the eight sectors of the implicit root.
        result.x |= ((child_index >> 2) & 1) << traversed_depth;
        result.y |= ((child_index >> 1) & 1) << traversed_depth;
        result.z |= (child_index & 1) << traversed_depth;
        ++traversed_depth;
    }
    WBE_SHADER_DEBUG_ASSERT(parent_index < 0 && traversed_depth <= p_tree_info.max_depth,
        ShaderDebugError::SVO_DEEPER_THAN_EXPECTED, p_frame_slot);
    WBE_SHADER_DEBUG_ASSERT(parent_index >= 0 || traversed_depth >= p_tree_info.max_depth,
        ShaderDebugError::SVO_SHALLOWER_THAN_EXPECTED, p_frame_slot);
    return result;
}
#endif

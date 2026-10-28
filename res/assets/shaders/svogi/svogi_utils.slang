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
#ifndef WBE_FILE_SVOGI_UTILS_SLANG
#define WBE_FILE_SVOGI_UTILS_SLANG

#include "svo/svo.slang"

struct SVOGIData {
    /**
     * @brief RGB color, alpha used for opacity.
     */
    float4 color;
    /**
     * @brief The average vector. The length encodes the variance: var = (1 - |average_vector|) / |average_vector|.
     */
    float3 average_vector;
};

/**
 * @brief Inject the occupancy of one leaf.
 * @details Writes 1 to the element of a leaf that svo.is_leaf_occupied() reports; nothing is written for an unoccupied or
 * superseded leaf, or for an index past svo.get_total_leaves().
 * @param p_svo_occupancy The SVO with its occupancy buffer, indexed by data index.
 * @param p_leaf_index Index of the leaf.
 * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
 */
void inject_svo_leaf_occupancy(SVOWriteBuffer<float> p_svo_occupancy, uint p_leaf_index, uint p_frame_slot) {
    SVO svo = p_svo_occupancy.svo;
    if (p_leaf_index < svo.get_total_leaves() && svo.is_leaf_occupied(p_leaf_index, p_frame_slot)) {
        p_svo_occupancy.write_data_at_index(svo.get_leaf(p_leaf_index).data_index, 1.0f, p_frame_slot);
    }
}

/**
 * @brief Inject the occupancy of the parent of one block of eight sibling leaves.
 * @details Writes the fraction of the parent's occupied sectors, from its children_data, to the parent's element, so it
 * does not depend on the leaves' elements. Nothing is written when the block lies past svo.get_total_leaves() or is a static
 * block superseded by a dynamic one, which still names its parent; only the referenced block writes a parent, so the
 * blocks may be injected concurrently.
 * @param p_svo_occupancy The SVO with its occupancy buffer, indexed by data index.
 * @param p_block_index Index of the block's first leaf; a multiple of eight.
 * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
 */
void inject_svo_leaf_parent_occupancy(SVOWriteBuffer<float> p_svo_occupancy, uint p_block_index, uint p_frame_slot) {
    SVO svo = p_svo_occupancy.svo;
    if (p_block_index >= svo.get_total_leaves()) {
        return;
    }
    SVONode parent = svo.get_node(uint(svo.get_leaf(p_block_index).parent_index));
    SVONode::ChildrenState expected_state =
        p_block_index >= svo.info.leaves_count ? SVONode::ChildrenState::LEAF_DYNAMIC : SVONode::ChildrenState::LEAF;
    if (parent.get_children_state() != expected_state || parent.get_children_index() != p_block_index) {
        return;
    }
    p_svo_occupancy.write_data_at_index(parent.data_index, float(parent.count_children_occupancy()) / 8.0f, p_frame_slot);
}

/**
 * @brief Propagate the occupancy of one block of eight sibling nodes to their parent.
 * @details Writes the average of the siblings' occupancy to the parent's element, reading a sibling's element only when it has
 * children, since only those were written; the others count as empty. Nothing is written when the block lies past
 * svo.get_total_nodes(), holds the root's sectors, is not at p_child_depth, or is a static block superseded by a dynamic
 * one, which still names its parent. Every sibling with children must already hold its occupancy, so the levels are
 * propagated from the deepest up, after the leaves' parents at depth svo.info.max_depth - 1 are written. Only the
 * referenced block writes a parent, so the blocks of one depth may be propagated concurrently.
 * @param p_svo_occupancy The SVO with its occupancy buffer, indexed by data index.
 * @param p_block_index Index of the block's first node; a multiple of eight.
 * @param p_child_depth Depth of the siblings to propagate, in [2, svo.info.max_depth - 1].
 * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
 */
void propagate_svo_occupancy(SVOWriteBuffer<float> p_svo_occupancy, uint p_block_index, uint p_child_depth,
    uint p_frame_slot) {
    SVO svo = p_svo_occupancy.svo;
    if (p_block_index >= svo.get_total_nodes()) {
        return;
    }
    int signed_parent_index = svo.get_node(p_block_index).parent_index;
    // The root's sectors have no stored parent.
    if (signed_parent_index < 0) {
        return;
    }
    if (svo.get_node_depth(p_block_index, p_frame_slot) != p_child_depth) {
        return;
    }
    SVONode parent = svo.get_node(uint(signed_parent_index));
    SVONode::ChildrenState expected_state =
        p_block_index >= svo.info.node_count ? SVONode::ChildrenState::NODE_DYNAMIC : SVONode::ChildrenState::NODE;
    if (parent.get_children_state() != expected_state || parent.get_children_index() != p_block_index) {
        return;
    }
    float occupancy_sum = 0.0f;
    [unroll] for (uint i = 0; i < 8; ++i) {
        SVONode child = svo.get_node(p_block_index + i);
        SVONode::ChildrenState child_state = child.get_children_state();
        if (child_state == SVONode::ChildrenState::NODE || child_state == SVONode::ChildrenState::NODE_DYNAMIC ||
            child_state == SVONode::ChildrenState::LEAF || child_state == SVONode::ChildrenState::LEAF_DYNAMIC) {
            occupancy_sum += p_svo_occupancy.read_data_at_index(child.data_index, p_frame_slot);
        }
    }
    p_svo_occupancy.write_data_at_index(parent.data_index, occupancy_sum / 8.0f, p_frame_slot);
}

#endif

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
/**
 * @file svo_common_functions.slang
 * @brief Test module exposing the SVO common, deferred-list, and dynamic-type helpers to slangpy calls.
 * @details svo/svo_common.slang relies on utils/utils.slang without including it, so this module includes both.
 * Helpers taking an SVONode are wrapped to take its packed children_data. No defines are required.
 */
#include "utils/utils.slang"
#include "svo/svo_dynamic_types.slang"

/**
 * @brief Decodes the ChildrenState of a node with the given packed description.
 */
uint svo_children_state(uint p_children_data) {
    SVONode node;
    node.children_data = p_children_data;
    return uint(get_svo_children_state(node));
}

/**
 * @brief Decodes the first child index of a node with the given packed description.
 */
uint svo_children_index(uint p_children_data) {
    SVONode node;
    node.children_data = p_children_data;
    return get_svo_children_index(node);
}

/**
 * @brief Returns (SVO_CHILDREN_OCCUPANCY_MASK, SVO_CHILDREN_BLOCK_INDEX_MASK, SVO_CHILDREN_PAYLOAD_MASK).
 */
uint3 svo_children_masks() {
    return uint3(SVO_CHILDREN_OCCUPANCY_MASK, SVO_CHILDREN_BLOCK_INDEX_MASK, SVO_CHILDREN_PAYLOAD_MASK);
}

/**
 * @brief Returns (SVO_CHILDREN_STATE_SHIFT, SVO_CHILDREN_OCCUPANCY_SHIFT, SVO_DEFERRED_LIST_COUNT_INDEX,
 * SVO_DEFERRED_LIST_ENTRY_OFFSET).
 */
uint4 svo_layout_constants() {
    return uint4(SVO_CHILDREN_STATE_SHIFT, SVO_CHILDREN_OCCUPANCY_SHIFT, SVO_DEFERRED_LIST_COUNT_INDEX,
        SVO_DEFERRED_LIST_ENTRY_OFFSET);
}

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
#ifndef WBE_FILE_SVO_DEFERRED_LIST_SLANG
#define WBE_FILE_SVO_DEFERRED_LIST_SLANG

/**
 * @brief Element of a deferred-list buffer holding the number of queued entries.
 */
static const uint SVO_DEFERRED_LIST_COUNT_INDEX = 0;
/**
 * @brief Element of a deferred-list buffer holding the first queued entry; entry i is stored at this offset plus i.
 */
static const uint SVO_DEFERRED_LIST_ENTRY_OFFSET = 1;

/**
 * @brief Traversal position retained across deferred construction rounds.
 */
struct SVONodeTraceInfo {
    /**
     * @brief Storage node_index refers to.
     */
    enum class IndexType {
        /**
         * @brief Index into the static node buffer.
         */
        STATIC_NODE = 0,
        /**
         * @brief Index into the dynamic node buffer, without the max_node_count virtual offset.
         */
        DYNAMIC_NODE = 1,
        /**
         * @brief Index into the static leaf buffer.
         */
        STATIC_LEAF = 2,
        /**
         * @brief Leaf slot in the dynamic leaf blocks; block local_index / 8, leaf local_index % 8.
         */
        DYNAMIC_LEAF = 3
    };
    /**
     * @brief Index of the node where traversal resumes.
     * @details traverse_svo() indexes the storage selected by index_type. Construction passes ignore index_type and
     * store a static or virtual node index.
     */
    uint node_index;
    /**
     * @brief Storage node_index refers to during traverse_svo().
     */
    IndexType index_type;
    /**
     * @brief Depth of the current stored node; root sectors have depth one.
     */
    uint curr_depth;
    /**
     * @brief Remaining traversal steps; negative means unbounded traversal.
     */
    int remain_depth;
    /**
     * @brief Voxel position in the current node's local unit cube.
     */
    float3 svo_position;
    /**
     * @brief Flattened coordinate of the traced voxel in the SVO's grid of extent 2^max_depth, packed as by
     * to_deferred_list_entry().
     * @details A node at depth d covers the voxels whose coordinate, shifted right by max_depth - d on each axis, equals
     * its cell, so the low max_depth - d bits of each axis are ignored for that node. Traversal does not change it; pass it
     * to SVO::voxel_index_to_global() with curr_depth to locate the reached node or leaf.
     */
    uint voxel_index;
};

/**
 * @brief Starts a traversal at the root sector containing a voxel.
 * @param p_svo_position Voxel position in the SVO's unit cube.
 * @param p_max_depth The SVO's max_depth.
 */
SVONodeTraceInfo get_svo_root_trace_info(float3 p_svo_position, uint p_max_depth) {
    SVONodeTraceInfo result;
    result.node_index = (uint(p_svo_position.x >= 0.5) << 2) | (uint(p_svo_position.y >= 0.5) << 1) | uint(p_svo_position.z >= 0.5);
    result.index_type = SVONodeTraceInfo::IndexType::STATIC_NODE;
    result.curr_depth = 1;
    result.remain_depth = int(p_max_depth) - 1;
    result.svo_position = frac(p_svo_position * 2.0);
    result.voxel_index = to_deferred_list_entry(p_svo_position, p_max_depth);
    return result;
}

/**
 * @brief Packs a voxel into a deferred-list entry, its flattened coordinate in the SVO's grid of extent 2^max_depth.
 * @details Each axis occupies max_depth bits, x in the lowest bits and z in the highest, so max_depth must not exceed 10.
 * Retries trace the entry again from the root.
 * @param p_svo_position Voxel position in the SVO's unit cube.
 * @param p_max_depth The SVO's max_depth.
 */
uint to_deferred_list_entry(float3 p_svo_position, uint p_max_depth) {
    uint extent = 1u << p_max_depth;
    uint3 voxel = min(uint3(p_svo_position * float(extent)), uint3(extent - 1));
    return voxel.x | (voxel.y << p_max_depth) | (voxel.z << (2 * p_max_depth));
}

/**
 * @brief Unpacks a deferred-list entry into its voxel's grid coordinate.
 * @param p_max_depth The SVO's max_depth used when packing p_entry.
 */
uint3 get_deferred_list_entry_voxel(uint p_entry, uint p_max_depth) {
    uint axis_mask = (1u << p_max_depth) - 1;
    return uint3(p_entry & axis_mask, (p_entry >> p_max_depth) & axis_mask, p_entry >> (2 * p_max_depth));
}

/**
 * @brief Creates a root traversal to the center of a deferred-list entry's voxel.
 * @param p_max_depth The SVO's max_depth used when packing p_entry.
 */
SVONodeTraceInfo to_trace_info(uint p_entry, uint p_max_depth) {
    float3 svo_position = (float3(get_deferred_list_entry_voxel(p_entry, p_max_depth)) + 0.5) / float(1u << p_max_depth);
    return get_svo_root_trace_info(svo_position, p_max_depth);
}

#endif

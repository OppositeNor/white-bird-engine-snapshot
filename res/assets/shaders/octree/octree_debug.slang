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
#ifndef WBE_FILE_OCTREE_DEBUG_SLANG
#define WBE_FILE_OCTREE_DEBUG_SLANG

#include "octree/octree_common.slang"

/**
 * @file octree_debug.slang
 * @brief Shared definitions of the octree debug draws.
 */

/**
 * @brief Indexed indirect draw command consumed by the debug wireframe draws.
 */
struct DrawIndexedIndirectCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

/**
 * @brief Index count of the cube drawn per debug instance.
 */
static const uint OCTREE_DEBUG_CUBE_INDEX_COUNT = 24;
/**
 * @brief Leaves along each axis of one leaf-view subgrid; one workgroup gathers each subgrid.
 */
static const uint3 OCTREE_DEBUG_SUBGRID_EXTENT = uint3(8, 8, 16);
/**
 * @brief Number of leaves in one leaf-view subgrid; each subgrid's occupancy mask has one bit per leaf.
 */
static const uint OCTREE_DEBUG_SUBGRID_LEAF_COUNT = 1024;
/**
 * @brief Invocations along each axis of the workgroup gathering one subgrid.
 */
static const uint3 OCTREE_DEBUG_SUBGRID_INVOCATION_EXTENT = uint3(4, 4, 8);
/**
 * @brief Leaves along each axis of the block one gather invocation tests; the block is one texel of mip level 1.
 */
static const uint3 OCTREE_DEBUG_SUBGRID_INVOCATION_LEAF_EXTENT = OCTREE_DEBUG_SUBGRID_EXTENT / OCTREE_DEBUG_SUBGRID_INVOCATION_EXTENT;
/**
 * @brief Number of 32-bit words in one subgrid's occupancy mask.
 */
static const uint OCTREE_DEBUG_SUBGRID_MASK_WORD_COUNT = OCTREE_DEBUG_SUBGRID_LEAF_COUNT / 32;

/**
 * @brief Occupancy mask of one leaf-view subgrid; bit i of the mask is bit i % 32 of word i / 32.
 */
struct OctreeDebugSubgridMask {
    /**
     * @brief Mask words packed four per vector, so the mask has a 16-byte storage-buffer stride.
     */
    uint4 words[OCTREE_DEBUG_SUBGRID_MASK_WORD_COUNT / 4];

    /**
     * @brief Gets one 32-bit word of the mask.
     */
    uint get_word(uint p_word_index) {
        return words[p_word_index / 4][p_word_index % 4];
    }
};
/**
 * @brief Element of the debug node list holding the number of gathered nodes, including nodes beyond its capacity.
 */
static const uint OCTREE_DEBUG_NODE_LIST_COUNT_INDEX = 0;
/**
 * @brief Element of the debug node list holding the first entry; entry i is stored at this offset plus i.
 */
static const uint OCTREE_DEBUG_NODE_LIST_ENTRY_OFFSET = 1;
/**
 * @brief Mip level whose texels are assigned one invocation each by the node gather.
 */
static const uint OCTREE_DEBUG_GATHER_LEVEL = 4;
/**
 * @brief Bit offset of the mip level in a debug node entry; the node texel occupies the lower bits, nine per axis.
 */
static const uint OCTREE_DEBUG_NODE_LEVEL_SHIFT = 27;

/**
 * @brief Gets the number of leaf-view subgrids along each axis of the leaf grid.
 * @param p_max_depth The octree's max_depth; the leaf grid must be divisible by OCTREE_DEBUG_SUBGRID_EXTENT.
 */
uint3 get_octree_debug_subgrid_grid_extent(uint p_max_depth) {
    return (1u << p_max_depth) / OCTREE_DEBUG_SUBGRID_EXTENT;
}

/**
 * @brief Gets the index of a leaf-view subgrid, which is also the index of its indirect draw command and occupancy mask.
 * @param p_subgrid Subgrid coordinate.
 * @param p_grid_extent Result of get_octree_debug_subgrid_grid_extent().
 */
uint get_octree_debug_subgrid_index(uint3 p_subgrid, uint3 p_grid_extent) {
    return p_subgrid.flatten(p_grid_extent.x, p_grid_extent.y);
}

/**
 * @brief Gets the subgrid coordinate of a leaf-view subgrid index.
 * @param p_index Result of get_octree_debug_subgrid_index().
 * @param p_grid_extent Result of get_octree_debug_subgrid_grid_extent().
 */
uint3 get_octree_debug_subgrid(uint p_index, uint3 p_grid_extent) {
    return uint3::unflatten(p_index, p_grid_extent.x, p_grid_extent.y);
}

/**
 * @brief Gets the leaf of a subgrid-local index, x varying fastest; the index is also the leaf's bit in the occupancy mask.
 * @param p_local_index Local index below OCTREE_DEBUG_SUBGRID_LEAF_COUNT.
 */
uint3 get_octree_debug_subgrid_leaf(uint p_local_index) {
    return uint3::unflatten(p_local_index, OCTREE_DEBUG_SUBGRID_EXTENT.x, OCTREE_DEBUG_SUBGRID_EXTENT.y);
}

/**
 * @brief Gets the subgrid-local index of a leaf, x varying fastest; the inverse of get_octree_debug_subgrid_leaf().
 * @param p_local_leaf Leaf coordinate inside its subgrid.
 */
uint get_octree_debug_subgrid_local_index(uint3 p_local_leaf) {
    return p_local_leaf.flatten(OCTREE_DEBUG_SUBGRID_EXTENT.x, OCTREE_DEBUG_SUBGRID_EXTENT.y);
}

/**
 * @brief Gets the position of the p_rank-th set bit of a subgrid's occupancy mask.
 * @param p_mask Occupancy mask.
 * @param p_rank Zero-based rank; must be below the mask's population count.
 */
uint get_octree_debug_set_bit(OctreeDebugSubgridMask p_mask, uint p_rank) {
    uint rank = p_rank;
    for (uint word_index = 0; word_index < OCTREE_DEBUG_SUBGRID_MASK_WORD_COUNT; ++word_index) {
        uint word = p_mask.get_word(word_index);
        uint word_count = countbits(word);
        if (rank < word_count) {
            for (uint i = 0; i < rank; ++i) {
                // Drop the lowest set bit.
                word &= word - 1;
            }
            return word_index * 32 + firstbitlow(word);
        }
        rank -= word_count;
    }
    return 0;
}

/**
 * @brief Packs a node of mip level one or higher into a debug node entry.
 * @param p_level Mip level of the node, at least one.
 * @param p_node Texel of the node, below 2^9 on each axis.
 */
uint to_octree_debug_node_entry(uint p_level, uint3 p_node) {
    return p_node.x | (p_node.y << 9) | (p_node.z << 18) | (p_level << OCTREE_DEBUG_NODE_LEVEL_SHIFT);
}

/**
 * @brief Unpacks a debug node entry.
 * @param p_entry Entry packed by to_octree_debug_node_entry().
 * @param p_level Receives the mip level of the node.
 * @return The texel of the node.
 */
uint3 get_octree_debug_node_entry(uint p_entry, out uint p_level) {
    p_level = p_entry >> OCTREE_DEBUG_NODE_LEVEL_SHIFT;
    return uint3(p_entry & 0x1ffu, (p_entry >> 9) & 0x1ffu, (p_entry >> 18) & 0x1ffu);
}

/**
 * @brief Gets the world-space corner of the cube of one texel.
 * @param p_info Octree configuration.
 * @param p_level Mip level of the texel.
 * @param p_texel Texel coordinate inside the level.
 * @param p_corner Selected corner, each component zero or one.
 */
float3 get_octree_texel_corner(OctreeInfo p_info, uint p_level, uint3 p_texel, uint3 p_corner) {
    float texel_width = p_info.width / float(1u << (p_info.max_depth - p_level));
    return p_info.origin + float3(p_texel + p_corner) * texel_width;
}

/**
 * @brief Selects one of the eight cube corners for a debug cube vertex.
 */
uint3 get_octree_debug_cube_corner(uint p_vertex_id) {
    return uint3((p_vertex_id >> 2) & 1, (p_vertex_id >> 1) & 1, p_vertex_id & 1);
}

#endif

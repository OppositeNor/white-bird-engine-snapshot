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
#ifndef WBE_FILE_OCTREE_COMMON_SLANG
#define WBE_FILE_OCTREE_COMMON_SLANG

#include "utils/utils.slang"

/**
 * @file octree_common.slang
 * @brief Layout of the octree stored in the mip chain of a uint8 3D texture.
 * @details Mip level 0 holds the leaves, a grid of extent 2^max_depth, and mip level l holds the nodes of depth
 * max_depth - l, so the single texel of level max_depth is the root. Every texel is OCTREE_EMPTY when nothing below it is
 * occupied, and any other value marks it occupied; the construction passes write OCTREE_OCCUPIED. A node is therefore
 * occupied exactly when one of its eight children is. The static scene and the dynamic meshes are stored in two separate
 * textures with the same layout. The construction occupies each covered leaf and its ancestors directly, without
 * recording which leaves it occupied.
 */

/**
 * @brief Texel value of a leaf or node without occupied geometry.
 */
static const uint OCTREE_EMPTY = 255;
/**
 * @brief Texel value the construction passes write into occupied leaves and their ancestors.
 */
static const uint OCTREE_OCCUPIED = 0;
/**
 * @brief Number of per-level storage-image descriptors bound by the construction passes; max_depth + 1 must not exceed it.
 */
static const uint OCTREE_LEVEL_DESCRIPTOR_COUNT = 10;
/**
 * @brief Occupancy flag of a node occupied by the static scene.
 */
static const uint OCTREE_OCCUPANCY_STATIC = 1;
/**
 * @brief Occupancy flag of a node occupied by the dynamic meshes.
 */
static const uint OCTREE_OCCUPANCY_DYNAMIC = 2;

/**
 * @brief Immutable spatial configuration and debug capacities.
 */
struct OctreeInfo {
    /**
     * @brief The origin of the octree in world space.
     */
    float3 origin;
    /**
     * @brief The width of the cubic region the octree covers.
     */
    float width;
    /**
     * @brief Depth of the leaves, at most ten; the textures have max_depth + 1 mip levels.
     */
    uint max_depth;
    /**
     * @brief Capacity of the debug node list in entries.
     */
    uint max_debug_node_count;
};

/**
 * @brief Converts a position in the octree's unit cube into its leaf-grid coordinate, clamped into the grid.
 * @param p_octree_position Position in the octree's unit cube.
 * @param p_max_depth The octree's max_depth.
 */
uint3 get_octree_voxel(float3 p_octree_position, uint p_max_depth) {
    uint extent = 1u << p_max_depth;
    return uint3(clamp(p_octree_position * float(extent), float3(0.0), float3(float(extent - 1))));
}

/**
 * @brief Gets the texel of the node at p_depth containing a leaf.
 * @param p_voxel Leaf-grid coordinate.
 * @param p_depth Depth of the node, at most p_max_depth.
 * @param p_max_depth The octree's max_depth.
 */
uint3 get_octree_node(uint3 p_voxel, uint p_depth, uint p_max_depth) {
    return p_voxel >> (p_max_depth - p_depth);
}

/**
 * @brief Read-write access to one value per texel of every mip level of an octree texture.
 */
interface IOctreeLevelStorage {
    /**
     * @brief Loads the value of one texel.
     * @param p_level Mip level of the texel.
     * @param p_texel Texel coordinate inside the level.
     */
    uint load(uint p_level, uint3 p_texel);
    /**
     * @brief Stores the value of one texel.
     * @param p_level Mip level of the texel.
     * @param p_texel Texel coordinate inside the level.
     * @param p_value Value stored into the texel.
     */
    void store(uint p_level, uint3 p_texel, uint p_value);
};

/**
 * @brief Stores p_value into the texels containing a leaf in the levels p_first_level to p_last_level, from the lowest level.
 * @details Stops at the first texel that already holds p_value. Within one pass every invocation stores only p_value into
 * these levels, so such a texel was stored by an invocation that continues to the levels above it, or held p_value before
 * the pass together with every level above it. A racing load observes either the value from before the pass or p_value, so a
 * stale load only repeats a store. The caller must guarantee that the levels above a texel holding p_value at the start of
 * the pass hold p_value too.
 * @param p_storage Texel storage of the octree texture.
 * @param p_voxel Leaf-grid coordinate.
 * @param p_first_level Lowest mip level written.
 * @param p_last_level Highest mip level written.
 * @param p_value OCTREE_OCCUPIED to occupy the texels, or OCTREE_EMPTY to clear them.
 */
void write_octree_ancestors<S : IOctreeLevelStorage>(
    S p_storage, uint3 p_voxel, uint p_first_level, uint p_last_level, uint p_value) {
    for (uint level = p_first_level; level <= p_last_level; ++level) {
        uint3 texel = p_voxel >> level;
        if (p_storage.load(level, texel) == p_value) {
            return;
        }
        p_storage.store(level, texel, p_value);
    }
}

/**
 * @brief Gets the OCTREE_OCCUPANCY_* flags of one texel.
 * @param p_static_levels Static octree texture, viewed with every mip level.
 * @param p_dynamic_levels Dynamic octree texture, viewed with every mip level; read only when p_include_dynamic is true.
 * @param p_include_dynamic Whether the dynamic occupancy is included.
 * @param p_level Mip level of the texel.
 * @param p_texel Texel coordinate inside the level.
 */
uint get_octree_occupancy(
    Texture3D<uint> p_static_levels, Texture3D<uint> p_dynamic_levels, bool p_include_dynamic, uint p_level, uint3 p_texel) {
    uint occupancy = 0;
    if (p_static_levels.Load(int4(int3(p_texel), int(p_level))) != OCTREE_EMPTY) {
        occupancy |= OCTREE_OCCUPANCY_STATIC;
    }
    if (p_include_dynamic && p_dynamic_levels.Load(int4(int3(p_texel), int(p_level))) != OCTREE_EMPTY) {
        occupancy |= OCTREE_OCCUPANCY_DYNAMIC;
    }
    return occupancy;
}

#endif

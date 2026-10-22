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
#ifndef WBE_FILE_OCTREE_SLANG
#define WBE_FILE_OCTREE_SLANG

#include "octree/octree_common.slang"

/**
 * @brief Read-only view of an octree stored in the mip chain of a 3D texture.
 * @details Mip level 0 holds the leaves and mip level l the nodes of depth info.max_depth - l. The view does not interpret
 * the texel values: the occupancy textures built by the octree construction store OCTREE_EMPTY or an occupied value, while
 * other textures with the same layout may store per-node data. The static scene and the dynamic meshes are separate
 * textures, so each needs its own view. The texture must not change during the calling pass.
 * @tparam T Texel type of the texture.
 */
struct Octree<T : ITexelElement> {
    /**
     * @brief The octree texture, viewed with every mip level.
     */
    Texture3D<T> levels;
    /**
     * @brief Spatial configuration of the octree.
     */
    OctreeInfo info;

    /**
     * @brief Create a view over an octree texture.
     */
    __init(Texture3D<T> p_levels, OctreeInfo p_info) {
        levels = p_levels;
        info = p_info;
    }

    /**
     * @brief Convert a world-space position into the octree's unit cube.
     * @param p_world_position Position in world space.
     * @return The position relative to info.origin, scaled by 1 / info.width.
     */
    float3 get_octree_position(float3 p_world_position) {
        return (p_world_position - info.origin) / info.width;
    }

    /**
     * @brief Get the value of the node at a depth that contains a position.
     * @details The value is returned whether or not the node is occupied.
     * @param p_world_position Position in world space; positions outside [info.origin, info.origin + info.width) are
     * clamped to the octree's boundary nodes.
     * @param p_depth Depth of the node, zero for the root and info.max_depth for a leaf; must not exceed info.max_depth.
     * @return The texel of the node in mip level info.max_depth - p_depth.
     */
    T trace(float3 p_world_position, uint p_depth) {
        uint3 node = get_octree_node(get_octree_voxel(get_octree_position(p_world_position), info.max_depth), p_depth, info.max_depth);
        return levels.Load(int4(int3(node), int(info.max_depth - p_depth)));
    }
};

#endif

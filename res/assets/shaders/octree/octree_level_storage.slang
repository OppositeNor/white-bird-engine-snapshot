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
#ifndef WBE_FILE_OCTREE_LEVEL_STORAGE_SLANG
#define WBE_FILE_OCTREE_LEVEL_STORAGE_SLANG

#include "octree/octree_common.slang"

/**
 * @brief Descriptor-heap reference to the single-level storage view of one mip level.
 * @details WBE_DYNAMIC_BUILD selects the dynamic octree texture instead of the static one.
 */
struct OctreeLevelDescriptorIndices {
#ifdef WBE_DYNAMIC_BUILD
    [WBE_DESCRIPTOR("storage_image", "octree_dynamic_levels[]")]
#else
    [WBE_DESCRIPTOR("storage_image", "octree_static_levels[]")]
#endif
    uint2 level;
};

/**
 * @brief Octree texel storage over the per-level storage views of the descriptor heap.
 */
struct OctreeHeapLevelStorage : IOctreeLevelStorage {
    /**
     * @brief Storage views indexed by mip level.
     */
    OctreeLevelDescriptorIndices levels[OCTREE_LEVEL_DESCRIPTOR_COUNT];

    uint load(uint p_level, uint3 p_texel) {
        RWTexture3D<uint> level = ResourceDescriptorHeap[levels[p_level].level.x];
        return level[p_texel];
    }

    void store(uint p_level, uint3 p_texel, uint p_value) {
        RWTexture3D<uint> level = ResourceDescriptorHeap[levels[p_level].level.x];
        level[p_texel] = p_value;
    }
};

#endif

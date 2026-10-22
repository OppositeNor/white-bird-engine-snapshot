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
/**
 * @file octree_write_ancestors.slang
 * @brief Test kernel that runs write_octree_ancestors() once per leaf entry over a mip pyramid stored in a buffer.
 * @details Required defines: MAX_DEPTH, FIRST_LEVEL, and VALUE. pyramid stores every level from level 0 upward, each level
 * x-major as x + y * extent + z * extent^2; voxels hold one leaf-grid coordinate each.
 */
#include "utils/utils.slang"
#include "octree/octree_common.slang"

RWStructuredBuffer<uint> pyramid;
StructuredBuffer<uint4> voxels;

/**
 * @brief Texel storage over the pyramid buffer.
 */
struct BufferLevelStorage : IOctreeLevelStorage {
    uint get_texel_index(uint p_level, uint3 p_texel) {
        uint offset = 0;
        for (uint level = 0; level < p_level; ++level) {
            uint level_extent = 1u << (MAX_DEPTH - level);
            offset += level_extent * level_extent * level_extent;
        }
        uint extent = 1u << (MAX_DEPTH - p_level);
        return offset + p_texel.x + p_texel.y * extent + p_texel.z * extent * extent;
    }

    uint load(uint p_level, uint3 p_texel) {
        return pyramid[get_texel_index(p_level, p_texel)];
    }

    void store(uint p_level, uint3 p_texel, uint p_value) {
        pyramid[get_texel_index(p_level, p_texel)] = p_value;
    }
};

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint voxel_count;
    uint voxel_stride;
    voxels.GetDimensions(voxel_count, voxel_stride);
    uint index = p_dispatch_thread_id.x;
    if (index >= voxel_count) {
        return;
    }
    BufferLevelStorage storage;
    write_octree_ancestors(storage, voxels[index].xyz, FIRST_LEVEL, MAX_DEPTH, VALUE);
}

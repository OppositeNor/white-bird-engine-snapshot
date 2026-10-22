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
 * @file octree_debug_subgrid.slang
 * @brief Test kernel for the leaf-view subgrid helpers of octree/octree_debug.slang.
 * @details Required define: MAX_DEPTH. Invocation i resolves the rank ranks[i] in masks[i] with get_octree_debug_set_bit(),
 * maps that bit to its subgrid-local leaf and back, and round-trips subgrid index i through get_octree_debug_subgrid().
 */
#include "utils/utils.slang"
#include "octree/octree_debug.slang"

StructuredBuffer<OctreeDebugSubgridMask> masks;
StructuredBuffer<uint> ranks;
// (set bit, leaf.x, leaf.y, leaf.z) per invocation.
RWStructuredBuffer<uint4> bits;
// Local index recomputed from the leaf per invocation.
RWStructuredBuffer<uint> local_indices;
// (subgrid.x, subgrid.y, subgrid.z, index recomputed from the subgrid) per invocation.
RWStructuredBuffer<uint4> subgrids;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint count;
    uint stride;
    ranks.GetDimensions(count, stride);
    uint index = p_dispatch_thread_id.x;
    if (index >= count) {
        return;
    }
    uint bit = get_octree_debug_set_bit(masks[index], ranks[index]);
    uint3 leaf = get_octree_debug_subgrid_leaf(bit);
    bits[index] = uint4(bit, leaf);
    local_indices[index] = get_octree_debug_subgrid_local_index(leaf);
    uint3 grid_extent = get_octree_debug_subgrid_grid_extent(MAX_DEPTH);
    uint3 subgrid = get_octree_debug_subgrid(index, grid_extent);
    subgrids[index] = uint4(subgrid, get_octree_debug_subgrid_index(subgrid, grid_extent));
}

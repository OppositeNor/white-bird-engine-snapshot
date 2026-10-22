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
 * @file octree_trace_data.slang
 * @brief Test kernel that runs Octree::trace() once per world-space query position and writes the traced values.
 * @details Required defines: TEXEL_TYPE, the texel type of the octree texture, and DEPTH, the traced depth.
 */
#include "utils/utils.slang"
#include "octree/octree.slang"

Texture3D<TEXEL_TYPE> levels;
RWStructuredBuffer<OctreeInfo> tree_info;
StructuredBuffer<float4> positions;
RWStructuredBuffer<TEXEL_TYPE> results;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint position_count;
    uint position_stride;
    positions.GetDimensions(position_count, position_stride);
    uint index = p_dispatch_thread_id.x;
    if (index >= position_count) {
        return;
    }
    Octree<TEXEL_TYPE> octree = Octree<TEXEL_TYPE>(levels, tree_info[0]);
    results[index] = octree.trace(positions[index].xyz, DEPTH);
}

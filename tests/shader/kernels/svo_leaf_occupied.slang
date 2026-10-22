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
 * @file svo_leaf_occupied.slang
 * @brief Test kernel that evaluates is_svo_leaf_occupied() for every allocated leaf slot.
 * @details Writes one result per element of occupied; no defines are required.
 */
#include "utils/utils.slang"
#include "svo/svo_common.slang"

StructuredBuffer<SVONode> nodes;
StructuredBuffer<SVOLeaf> leaves;
StructuredBuffer<SVOInfo> tree_info;
RWStructuredBuffer<uint> occupied;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint leaf_count;
    uint leaf_stride;
    occupied.GetDimensions(leaf_count, leaf_stride);
    uint index = p_dispatch_thread_id.x;
    if (index >= leaf_count) {
        return;
    }
    occupied[index] = is_svo_leaf_occupied(index, nodes, leaves, tree_info[0], 0) ? 1 : 0;
}

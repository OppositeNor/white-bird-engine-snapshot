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
 * @file svo_leaf_queries.slang
 * @brief Test kernel that evaluates SVO::is_leaf_occupied() and SVO::get_leaf_grid_position() for every leaf index, and
 * writes SVO::get_total_leaves() and SVO::get_total_nodes().
 * @details Dispatch one thread per leaf index below SVO::get_total_leaves(); no defines are required.
 */
#include "utils/utils.slang"
#include "svo/svo.slang"

StructuredBuffer<SVONode> nodes;
StructuredBuffer<SVOLeaf> leaves;
StructuredBuffer<SVOInfo> tree_info;
RWStructuredBuffer<uint> occupied;
RWStructuredBuffer<uint4> grid_positions;
RWStructuredBuffer<uint2> totals;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    SVO svo = SVO(nodes, leaves, tree_info[0], true);
    uint index = p_dispatch_thread_id.x;
    if (index == 0) {
        totals[0] = uint2(svo.get_total_leaves(), svo.get_total_nodes());
    }
    if (index >= svo.get_total_leaves()) {
        return;
    }
    bool is_occupied = svo.is_leaf_occupied(index, 0);
    occupied[index] = is_occupied ? 1 : 0;
    // Only occupied leaves are guaranteed to be referenced by their parent, which the position walk requires.
    grid_positions[index] = is_occupied ? uint4(svo.get_leaf_grid_position(index, 0), 0) : uint4(0);
}

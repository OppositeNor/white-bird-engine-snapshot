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
 * @file svogi_occupancy_injection.slang
 * @brief Test kernel that runs inject_svo_leaf_occupancy() for every leaf, or inject_svo_leaf_parent_occupancy() for
 * every block of eight leaves.
 * @details INJECT_PARENTS selects the function: 0 for the leaves, dispatched with one thread per leaf, and 1 for the
 * parents, dispatched with one thread per block. Threads from invocation_count on do nothing, so a dispatch may cover a
 * prefix of the leaves or blocks.
 */
#include "svogi/svogi_utils.slang"

StructuredBuffer<SVONode> nodes;
StructuredBuffer<SVOLeaf> leaves;
StructuredBuffer<SVOInfo> tree_info;
RWStructuredBuffer<float> occupancy;
uniform uint invocation_count;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    if (p_dispatch_thread_id.x >= invocation_count) {
        return;
    }
    SVOWriteBuffer<float> svo_occupancy = SVOWriteBuffer<float>(SVO(nodes, leaves, tree_info[0], true), occupancy);
#if INJECT_PARENTS
    inject_svo_leaf_parent_occupancy(svo_occupancy, p_dispatch_thread_id.x * 8, 0);
#else
    inject_svo_leaf_occupancy(svo_occupancy, p_dispatch_thread_id.x, 0);
#endif
}

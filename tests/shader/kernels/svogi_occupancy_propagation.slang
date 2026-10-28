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
 * @file svogi_occupancy_propagation.slang
 * @brief Test kernel that runs propagate_svo_occupancy() for every block of eight nodes at child_depth.
 * @details Dispatch one thread per block of SVO::get_total_nodes(); no defines are required.
 */
#include "svogi/svogi_utils.slang"

StructuredBuffer<SVONode> nodes;
StructuredBuffer<SVOLeaf> leaves;
StructuredBuffer<SVOInfo> tree_info;
RWStructuredBuffer<float> occupancy;
uniform uint child_depth;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    SVOWriteBuffer<float> svo_occupancy = SVOWriteBuffer<float>(SVO(nodes, leaves, tree_info[0], true), occupancy);
    propagate_svo_occupancy(svo_occupancy, p_dispatch_thread_id.x * 8, child_depth, 0);
}

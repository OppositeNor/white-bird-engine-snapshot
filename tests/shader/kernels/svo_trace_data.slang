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
 * @file svo_trace_data.slang
 * @brief Test kernel that runs SVO::trace_data() once per world-space query position and writes the data indices and final
 * traces.
 * @details Required define: TRAVERSE_DYNAMIC, the traverse_dynamic flag of the SVO.
 */
#include "utils/utils.slang"
#include "svo/svo.slang"

RWStructuredBuffer<SVONode> nodes;
RWStructuredBuffer<SVONode> dynamic_nodes;
StructuredBuffer<SVOLeaf> leaves;
StructuredBuffer<SVODynamicLeafBlock> dynamic_leaves;
RWStructuredBuffer<SVOInfo> tree_info;
StructuredBuffer<float4> positions;
RWStructuredBuffer<uint> results;
RWStructuredBuffer<SVONodeTraceInfo> traces;

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
    SVO svo = SVO(nodes, dynamic_nodes, leaves, dynamic_leaves, tree_info[0], TRAVERSE_DYNAMIC != 0);
    SVONodeTraceInfo trace_info;
    results[index] = svo.trace_data(positions[index].xyz, trace_info);
    traces[index] = trace_info;
}

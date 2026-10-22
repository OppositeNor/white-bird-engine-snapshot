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
 * @file svo_enlarged_edge_line.slang
 * @brief Test kernel that evaluates get_enlarged_edge_line() from svo_3/svo_gather_leaves.slang for every edge.
 * @details edges holds (start, end) per edge and orientations the projected triangle's orientation; lines receives the
 * homogeneous line. No defines are required.
 */
#include "svo_3/svo_gather_leaves.slang"

RWStructuredBuffer<float4> edges;
RWStructuredBuffer<float> orientations;
RWStructuredBuffer<float4> lines;

[shader("compute")]
[numthreads(32, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint edge_count;
    uint edge_stride;
    lines.GetDimensions(edge_count, edge_stride);
    uint index = p_dispatch_thread_id.x;
    if (index >= edge_count) {
        return;
    }
    lines[index] = float4(get_enlarged_edge_line(edges[index].xy, edges[index].zw, orientations[index]), 0.0);
}

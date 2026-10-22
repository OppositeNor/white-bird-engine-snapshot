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
 * @file workgroup_inclusive_prefix_sum_64_256.slang
 * @brief Test kernel that runs workgroup_inclusive_prefix_sum_64_256 in place over a 256-element buffer on one 64-wide
 * workgroup.
 */
#include "prefix_sum/prefix_sum_utils.slang"

RWStructuredBuffer<uint> data;
groupshared uint shared_data[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    for (uint i = p_group_thread_id.x; i < 256; i += 64) {
        shared_data[i] = data[i];
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_prefix_sum_64_256(shared_data, p_group_thread_id.x * 4);
    for (uint i = p_group_thread_id.x; i < 256; i += 64) {
        data[i] = shared_data[i];
    }
}

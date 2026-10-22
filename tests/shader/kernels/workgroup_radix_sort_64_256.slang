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
 * @file workgroup_radix_sort_64_256.slang
 * @brief Test kernel that sorts a 256-element buffer in place on one 64-wide workgroup.
 * @details Runs workgroup_radix_sort_step_64_256 on the digit at BIT_START when BIT_START is defined, and
 * workgroup_radix_sort_64_256 otherwise.
 */
#include "sort/radix_sort_utils.slang"

RWStructuredBuffer<uint> data;
groupshared uint shared_data[256];
groupshared uint shared_scratch[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    for (uint i = p_group_thread_id.x; i < 256; i += 64) {
        shared_data[i] = data[i];
    }
    GroupMemoryBarrierWithGroupSync();
#ifdef BIT_START
    workgroup_radix_sort_step_64_256(shared_data, BIT_START, p_group_thread_id.x, shared_scratch);
#else
    workgroup_radix_sort_64_256(shared_data, p_group_thread_id.x, shared_scratch);
#endif
    for (uint i = p_group_thread_id.x; i < 256; i += 64) {
        data[i] = shared_data[i];
    }
}

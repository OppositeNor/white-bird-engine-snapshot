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
 * @file workgroup_bitonic_sort_256_1024.slang
 * @brief Test kernel that runs workgroup_bitonic_sort_256_1024 in place over a 1024-element buffer on one 256-wide
 * workgroup.
 * @details WBE_TEST_KEY_TYPE selects the key type, uint or uint2.
 */
#include "sort/bitonic_sort_utils.slang"

RWStructuredBuffer<WBE_TEST_KEY_TYPE> data;
groupshared WBE_TEST_KEY_TYPE shared_data[1024];

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    for (uint i = p_group_thread_id.x; i < 1024; i += 256) {
        shared_data[i] = data[i];
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_bitonic_sort_256_1024(shared_data, p_group_thread_id.x);
    for (uint i = p_group_thread_id.x; i < 1024; i += 256) {
        data[i] = shared_data[i];
    }
}

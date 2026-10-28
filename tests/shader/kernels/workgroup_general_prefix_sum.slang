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
 * @file workgroup_general_prefix_sum.slang
 * @brief Test kernel that runs workgroup_general_prefix_sum over a range of data on one workgroup.
 * @details Required defines: KERNEL_WORKGROUP_SIZE, DATA_START, and DATA_COUNT.
 */
#include "prefix_sum/prefix_sum_utils.slang"

RWStructuredBuffer<uint> data;
groupshared uint scratch[KERNEL_WORKGROUP_SIZE];

[shader("compute")]
[numthreads(KERNEL_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    workgroup_general_prefix_sum<uint, Add<uint>, KERNEL_WORKGROUP_SIZE>(data, DATA_START, DATA_COUNT, p_group_thread_id.x,
        scratch);
}

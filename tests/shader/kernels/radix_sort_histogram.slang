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
 * @file radix_sort_histogram.slang
 * @brief Test kernel that accumulates two ranges with workgroup_scope_radix_sort_histogram on one workgroup.
 * @details Loads the initial histogram from histogram, adds the counts of [FIRST_DATA_START, FIRST_DATA_START +
 * FIRST_DATA_COUNT) and then of [SECOND_DATA_START, SECOND_DATA_START + SECOND_DATA_COUNT), and writes the histogram
 * back. Required defines: ELEMENT_TYPE, KERNEL_WORKGROUP_SIZE, FIRST_DATA_START, FIRST_DATA_COUNT, SECOND_DATA_START,
 * SECOND_DATA_COUNT, and WBE_RADIX_HISTOGRAM_BIT_REGION.
 */
#include "sort/radix_sort_utils.slang"

RWStructuredBuffer<ELEMENT_TYPE> data;
RWStructuredBuffer<uint> histogram;
groupshared uint shared_histogram[WBE_RADIX_SORT_BUCKET_COUNT];

[shader("compute")]
[numthreads(KERNEL_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    // No barrier follows the load: the call must synchronize before counting so that every initial value is visible.
    for (uint i = p_group_thread_id.x; i < WBE_RADIX_SORT_BUCKET_COUNT; i += KERNEL_WORKGROUP_SIZE) {
        shared_histogram[i] = histogram[i];
    }
    workgroup_scope_radix_sort_histogram<ELEMENT_TYPE, KERNEL_WORKGROUP_SIZE>(data, FIRST_DATA_START, FIRST_DATA_COUNT,
        shared_histogram, p_group_thread_id.x, WBE_RADIX_HISTOGRAM_BIT_REGION);
    workgroup_scope_radix_sort_histogram<ELEMENT_TYPE, KERNEL_WORKGROUP_SIZE>(data, SECOND_DATA_START, SECOND_DATA_COUNT,
        shared_histogram, p_group_thread_id.x, WBE_RADIX_HISTOGRAM_BIT_REGION);
    // Each invocation writes back counters accumulated by other invocations, which relies on the trailing barrier.
    for (uint i = p_group_thread_id.x; i < WBE_RADIX_SORT_BUCKET_COUNT; i += KERNEL_WORKGROUP_SIZE) {
        histogram[i] = shared_histogram[i];
    }
}

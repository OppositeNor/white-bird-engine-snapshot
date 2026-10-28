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
 * @file radix_sort_uint2.slang
 * @brief Test kernel that runs radix_sort_uint2_histogram() or radix_sort_uint2_scatter() from
 * sort/radix_sort_uint2_utils.slang, one local sort per workgroup.
 * @details Required defines: KERNEL_PASS, 0 for the histogram pass and 1 for the scatter pass, and
 * WBE_RADIX_HISTOGRAM_BIT_REGION.
 */
#include "sort/radix_sort_uint2_utils.slang"

RWByteAddressBuffer input_keys;
RWStructuredBuffer<uint> histogram_buffer;
RWByteAddressBuffer output_keys;

groupshared uint histogram[WBE_RADIX_SORT_BUCKET_COUNT];
groupshared uint sort_entries[WBE_RADIX_SORT_UINT2_BATCH_SIZE];
groupshared uint sort_scratch[WBE_RADIX_SORT_UINT2_BATCH_SIZE];
groupshared uint2 batch_keys[WBE_RADIX_SORT_UINT2_BATCH_SIZE];
groupshared uint digit_offsets[WBE_RADIX_SORT_BUCKET_COUNT];
groupshared uint batch_digit_starts[WBE_RADIX_SORT_BUCKET_COUNT];

[shader("compute")]
[numthreads(WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    CountedBuffer<uint2> input = CountedBuffer<uint2>(input_keys, 0);
    CountedBuffer<uint2> output = CountedBuffer<uint2>(output_keys, 0);
#if KERNEL_PASS == 0
    radix_sort_uint2_histogram(input, histogram_buffer, histogram, p_group_id.x, p_group_thread_id.x,
        WBE_RADIX_HISTOGRAM_BIT_REGION);
#else
    radix_sort_uint2_scatter(input, histogram_buffer, output, sort_entries, sort_scratch, batch_keys, digit_offsets,
        batch_digit_starts, p_group_id.x, p_group_thread_id.x, WBE_RADIX_HISTOGRAM_BIT_REGION);
#endif
}

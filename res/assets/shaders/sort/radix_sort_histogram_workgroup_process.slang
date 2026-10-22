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
#include "utils/utils.slang"
#include "sort/radix_sort_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "input_buffer", "read")] uint2 input_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "histogram_buffer", "write")] uint2 histogram_buffer;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

#define WORKGROUP_SIZE 64
// The sub-histograms start after the metadata of the histogram buffer.
#define LANE_START 4

groupshared uint histogram[WBE_RADIX_SORT_BUCKET_COUNT];

// Counts the digit at WBE_RADIX_HISTOGRAM_BIT_REGION of every key. Workgroup g owns the contiguous key range
// [g * elements_per_group, (g + 1) * elements_per_group), with elements_per_group = ceil(element_count / sub_histogram_num),
// and writes the raw digit counts of that range to sub-histogram g. The input buffer holds the element count and the local sort count in its
// metadata, and the keys from WBE_RADIX_SORT_DATA_START. The histogram buffer receives its metadata: the sub-histogram count,
// equal to the local sort count; the start of the overflow histograms, after the last sub-histogram; and LANE_START, where
// sub-histogram g starts at LANE_START + g * WBE_RADIX_SORT_BUCKET_COUNT. Must be dispatched with the local sort dispatch.
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> input_buffer = ResourceDescriptorHeap[descriptor_indices.input_buffer.x];
    RWStructuredBuffer<uint> histogram_buffer = ResourceDescriptorHeap[descriptor_indices.histogram_buffer.x];
    uint group_id = p_group_id.x;
    uint group_thread_id = p_group_thread_id.x;
    // The starting position of a thread in a 256 chunk.
    uint thread_data_start = group_thread_id * 4;
    uint element_count = input_buffer[WBE_RADIX_SORT_ELEMENT_COUNT_INDEX];
    uint sub_histogram_num = input_buffer[WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX];
    if (group_id >= sub_histogram_num) {
        return;
    }
    if (group_id == 0 && group_thread_id == 0) {
        histogram_buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX] = sub_histogram_num;
        histogram_buffer[WBE_RADIX_SORT_OVERFLOW_START_INDEX] = LANE_START + sub_histogram_num * WBE_RADIX_SORT_BUCKET_COUNT;
        histogram_buffer[WBE_RADIX_SORT_LANE_START_INDEX] = LANE_START;
    }
    [unroll] for (uint i = 0; i < 4; ++i) {
        // Initialize the original value in the histogram to 0.
        histogram[thread_data_start + i] = 0;
    }

    uint elements_per_group = radix_sort_elements_per_local_sort(element_count, sub_histogram_num);
    uint group_begin = min(group_id * elements_per_group, element_count);
    uint group_end = min(group_begin + elements_per_group, element_count);
    // Synchronizes before counting, so the zeroed histogram is visible.
    workgroup_scope_radix_sort_histogram<uint, WORKGROUP_SIZE>(input_buffer, WBE_RADIX_SORT_DATA_START + group_begin,
        group_end - group_begin, histogram, group_thread_id, WBE_RADIX_HISTOGRAM_BIT_REGION);

    uint sub_histogram_start = LANE_START + group_id * WBE_RADIX_SORT_BUCKET_COUNT;
    [unroll] for (uint i = 0; i < 4; ++i) {
        histogram_buffer[sub_histogram_start + thread_data_start + i] = histogram[thread_data_start + i];
    }
}

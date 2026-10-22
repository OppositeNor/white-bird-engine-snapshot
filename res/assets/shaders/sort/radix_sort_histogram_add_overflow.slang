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
    [WBE_DESCRIPTOR("storage_buffer", "histogram_buffer", "read_write")] uint2 histogram_buffer;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// One invocation per bucket.
#define WORKGROUP_SIZE WBE_RADIX_SORT_BUCKET_COUNT

// Last step of the global prefix sum of the sub-histograms. Workgroup g adds to sub-histogram g the exclusive prefix sum of
// the overflow histograms at its scan group w = g / WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM: the inclusive one of overflow
// histogram w - 1 from radix_sort_histogram_overflow_prefix_sum, or 0 for the first scan group. Entry b of sub-histogram g
// then holds the sum of entry b of sub-histograms [0, g]. Must be dispatched with the local sort dispatch.
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> buffer = ResourceDescriptorHeap[descriptor_indices.histogram_buffer.x];
    uint sub_histogram_num = buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX];
    uint overflow_start = buffer[WBE_RADIX_SORT_OVERFLOW_START_INDEX];
    uint lane_start = buffer[WBE_RADIX_SORT_LANE_START_INDEX];
    uint sub_histogram = p_group_id.x;
    uint bucket = p_group_thread_id.x;
    uint scan_group = sub_histogram / WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM;
    if (sub_histogram >= sub_histogram_num || scan_group == 0) {
        return;
    }
    buffer[lane_start + sub_histogram * WBE_RADIX_SORT_BUCKET_COUNT + bucket] +=
        buffer[overflow_start + (scan_group - 1) * WBE_RADIX_SORT_BUCKET_COUNT + bucket];
}

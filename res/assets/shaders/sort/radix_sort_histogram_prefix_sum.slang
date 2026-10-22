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

groupshared uint histograms[WBE_RADIX_SORT_SCAN_ENTRY_NUM];

// First step of the global prefix sum of the sub-histograms. Workgroup w scans the WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM
// sub-histograms starting at w * WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM in groupshared memory, so that entry b of each becomes the
// sum of entry b over the earlier sub-histograms of the same scan group and itself. Sub-histograms past the last one count
// as zero. The last scanned entry of every bucket is the total of the scan group, written to overflow histogram w at
// overflow_start + w * WBE_RADIX_SORT_BUCKET_COUNT. radix_sort_histogram_overflow_prefix_sum and
// radix_sort_histogram_add_overflow then add the totals of the earlier scan groups. Must be dispatched with the histogram
// prefix scan dispatch: ceil(sub_histogram_num / WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM) workgroups.
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> buffer = ResourceDescriptorHeap[descriptor_indices.histogram_buffer.x];
    uint sub_histogram_num = buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX];
    uint overflow_start = buffer[WBE_RADIX_SORT_OVERFLOW_START_INDEX];
    uint lane_start = buffer[WBE_RADIX_SORT_LANE_START_INDEX];
    uint group_id = p_group_id.x;
    uint bucket = p_group_thread_id.x;
    uint first_sub_histogram = group_id * WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM;
    [unroll] for (uint i = 0; i < WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM; ++i) {
        uint sub_histogram = first_sub_histogram + i;
        histograms[bucket + i * WBE_RADIX_SORT_BUCKET_COUNT] = sub_histogram < sub_histogram_num
            ? buffer[lane_start + sub_histogram * WBE_RADIX_SORT_BUCKET_COUNT + bucket]
            : 0;
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_histogram_scan(histograms, bucket);
    [unroll] for (uint i = 0; i < WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM; ++i) {
        uint sub_histogram = first_sub_histogram + i;
        if (sub_histogram < sub_histogram_num) {
            buffer[lane_start + sub_histogram * WBE_RADIX_SORT_BUCKET_COUNT + bucket] =
                histograms[bucket + i * WBE_RADIX_SORT_BUCKET_COUNT];
        }
    }
    buffer[overflow_start + group_id * WBE_RADIX_SORT_BUCKET_COUNT + bucket] =
        histograms[bucket + (WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM - 1) * WBE_RADIX_SORT_BUCKET_COUNT];
}

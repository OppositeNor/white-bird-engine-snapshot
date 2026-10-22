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

// Second step of the global prefix sum of the sub-histograms. Computes the inclusive prefix sum across the overflow
// histograms from radix_sort_histogram_prefix_sum, separately for every bucket, so that entry b of overflow histogram w
// becomes the sum of entry b of every sub-histogram of scan groups [0, w]. The overflow histograms are scanned in segments
// of WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM in groupshared memory, and each segment adds the running total of the earlier ones.
// Must be dispatched with exactly one workgroup.
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> buffer = ResourceDescriptorHeap[descriptor_indices.histogram_buffer.x];
    uint sub_histogram_num = buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX];
    uint overflow_start = buffer[WBE_RADIX_SORT_OVERFLOW_START_INDEX];
    uint overflow_num = (sub_histogram_num + WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM - 1) / WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM;
    uint bucket = p_group_thread_id.x;
    // The sum of this bucket over the overflow histograms of the earlier segments.
    uint running_sum = 0;
    for (uint segment_start = 0; segment_start < overflow_num; segment_start += WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM) {
        [unroll] for (uint i = 0; i < WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM; ++i) {
            uint overflow = segment_start + i;
            histograms[bucket + i * WBE_RADIX_SORT_BUCKET_COUNT] = overflow < overflow_num
                ? buffer[overflow_start + overflow * WBE_RADIX_SORT_BUCKET_COUNT + bucket]
                : 0;
        }
        GroupMemoryBarrierWithGroupSync();
        workgroup_inclusive_histogram_scan(histograms, bucket);
        [unroll] for (uint i = 0; i < WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM; ++i) {
            uint overflow = segment_start + i;
            if (overflow < overflow_num) {
                buffer[overflow_start + overflow * WBE_RADIX_SORT_BUCKET_COUNT + bucket] =
                    running_sum + histograms[bucket + i * WBE_RADIX_SORT_BUCKET_COUNT];
            }
        }
        running_sum += histograms[bucket + (WBE_RADIX_SORT_SCAN_HISTOGRAM_NUM - 1) * WBE_RADIX_SORT_BUCKET_COUNT];
        // Every invocation has read the scanned segment before the next one overwrites it.
        GroupMemoryBarrierWithGroupSync();
    }
}

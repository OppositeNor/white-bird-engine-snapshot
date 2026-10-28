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
    [WBE_DESCRIPTOR("storage_buffer", "histogram_buffer", "read")] uint2 histogram_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "output_buffer", "write")] uint2 output_buffer;
    [WBE_PUSH_CONSTANT("frame_slot")] uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

#define WORKGROUP_SIZE 64
// The number of elements sorted in groupshared memory at once.
#define BATCH_SIZE 256

groupshared uint sort_keys[BATCH_SIZE];
groupshared uint sort_scratch[BATCH_SIZE];
// The output index the next element of each digit is written to.
groupshared uint digit_offsets[WBE_RADIX_SORT_BUCKET_COUNT];
// The index of the first element of each digit in the sorted batch.
groupshared uint batch_digit_starts[WBE_RADIX_SORT_BUCKET_COUNT];

// Scatters the elements of one sub-histogram into their sorted positions for the digit at WBE_RADIX_HISTOGRAM_BIT_REGION.
// Workgroup g owns the contiguous input range that sub-histogram g counted:
// [g * elements_per_group, (g + 1) * elements_per_group), with elements_per_group = ceil(element_count / sub_histogram_num).
// The histogram buffer must hold the column-wise inclusive prefix sum from radix_sort_histogram_add_overflow. Each element
// is written to: (number of elements with a smaller digit) + (number of elements with the same digit in earlier
// sub-histograms) + (rank among the elements with the same digit in this sub-histogram). The workgroup sorts its range
// stably by the digit in batches of 256, so the scatter is stable. The output buffer receives the element count and the
// local sort count of the input buffer. Must be dispatched with the local sort dispatch.
[shader("compute")]
[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;
    RWStructuredBuffer<uint> histogram_buffer = ResourceDescriptorHeap[descriptor_indices.histogram_buffer.x];
    CountedBuffer<uint> input_keys = CountedBuffer<uint>(descriptor_indices.input_buffer, frame_slot);
    CountedBuffer<uint> output_keys = CountedBuffer<uint>(descriptor_indices.output_buffer, frame_slot);
    uint group_id = p_group_id.x;
    uint group_thread_id = p_group_thread_id.x;
    uint thread_data_start = group_thread_id * 4;
    uint element_count = input_keys.size();
    uint sub_histogram_num = histogram_buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX];
    uint lane_start = histogram_buffer[WBE_RADIX_SORT_LANE_START_INDEX];
    if (group_id >= sub_histogram_num) {
        return;
    }
    if (group_id == 0 && group_thread_id == 0) {
        output_keys.set_size(element_count);
        output_keys.set_header(
            WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX, input_keys.get_header(WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX));
    }

    // The last sub-histogram holds the total count of each digit after the column-wise inclusive prefix sum. An exclusive
    // prefix sum over the digits gives the output index of the first element of each digit.
    uint last_sub_histogram_start = lane_start + (sub_histogram_num - 1) * WBE_RADIX_SORT_BUCKET_COUNT;
    [unroll] for (uint i = 0; i < 4; ++i) {
        sort_scratch[thread_data_start + i] = histogram_buffer[last_sub_histogram_start + thread_data_start + i];
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_prefix_sum_64_256<uint, Add<uint>>(sort_scratch, thread_data_start);
    [unroll] for (uint i = 0; i < 4; ++i) {
        uint digit = thread_data_start + i;
        // The index of the first digit underflows past the end, so it reads zero.
        uint previous_digit = digit - 1u;
        uint digit_start = previous_digit < BATCH_SIZE ? sort_scratch[previous_digit] : 0;
        // The inclusive prefix sum of the previous sub-histogram counts this digit in all earlier sub-histograms.
        uint earlier_sub_histogram_count = 0;
        if (group_id > 0) {
            earlier_sub_histogram_count =
                histogram_buffer[lane_start + (group_id - 1) * WBE_RADIX_SORT_BUCKET_COUNT + digit];
        }
        digit_offsets[digit] = digit_start + earlier_sub_histogram_count;
    }

    uint elements_per_group = radix_sort_elements_per_local_sort(element_count, sub_histogram_num);
    uint group_begin = min(group_id * elements_per_group, element_count);
    uint group_end = min(group_begin + elements_per_group, element_count);
    for (uint batch_begin = group_begin; batch_begin < group_end; batch_begin += BATCH_SIZE) {
        uint batch_count = min(group_end - batch_begin, BATCH_SIZE);
        // Padding keys have the largest digit, so the stable sort keeps them after every valid element.
        [unroll] for (uint i = 0; i < 4; ++i) {
            uint index = thread_data_start + i;
            sort_keys[index] =
                index < batch_count ? input_keys.get(batch_begin + index) : 0xFFFFFFFFu;
        }
        // Also orders the sort_scratch reads above before the sort overwrites it.
        GroupMemoryBarrierWithGroupSync();
        // Stably sort the batch by the WBE_RADIX_SORT_BUCKET_BIT_SIZE-bit digit only.
        [unroll] for (uint i = 0; i < WBE_RADIX_SORT_BUCKET_BIT_SIZE / WBE_RADIX_SORT_WG_BIT_SIZE; ++i) {
            workgroup_radix_sort_step_64_256(sort_keys, WBE_RADIX_HISTOGRAM_BIT_REGION + i * WBE_RADIX_SORT_WG_BIT_SIZE,
                group_thread_id, sort_scratch);
        }

        uint4 keys;
        uint4 digits;
        [unroll] for (uint i = 0; i < 4; ++i) {
            uint index = thread_data_start + i;
            keys[i] = sort_keys[index];
            digits[i] = radix_sort_digit(keys[i], WBE_RADIX_HISTOGRAM_BIT_REGION);
            if (index < batch_count
                && (index == 0 || radix_sort_digit(sort_keys[index - 1], WBE_RADIX_HISTOGRAM_BIT_REGION) != digits[i])) {
                batch_digit_starts[digits[i]] = index;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // The number of elements of a digit in the batch, held by the invocation owning the last element of that digit.
        uint4 batch_digit_counts = uint4(0);
        [unroll] for (uint i = 0; i < 4; ++i) {
            uint index = thread_data_start + i;
            if (index < batch_count) {
                uint rank = index - batch_digit_starts[digits[i]];
                output_keys.set(digit_offsets[digits[i]] + rank, keys[i]);
                if (index == batch_count - 1
                    || radix_sort_digit(sort_keys[index + 1], WBE_RADIX_HISTOGRAM_BIT_REGION) != digits[i]) {
                    batch_digit_counts[i] = rank + 1;
                }
            }
        }
        // Every invocation has read digit_offsets before it is advanced.
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint i = 0; i < 4; ++i) {
            if (batch_digit_counts[i] > 0) {
                digit_offsets[digits[i]] += batch_digit_counts[i];
            }
        }
        // The next batch overwrites sort_keys and reads digit_offsets.
        GroupMemoryBarrierWithGroupSync();
    }
}

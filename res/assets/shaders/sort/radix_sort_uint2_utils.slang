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
#ifndef WBE_FILE_RADIX_SORT_UINT2_UTILS_SLANG
#define WBE_FILE_RADIX_SORT_UINT2_UTILS_SLANG

#include "sort/radix_sort_utils.slang"
#include "prefix_sum/prefix_sum_utils.slang"

// The uint2 radix sort orders uint2 keys lexicographically, by x and then by y, i.e. as the 64-bit value (x << 32) | y,
// whose bits [0, 32) are y and bits [32, 64) are x. Each pass sorts stably by one WBE_RADIX_SORT_BUCKET_BIT_SIZE-bit digit,
// so sorting only the digits of x orders the keys by x and carries y as a payload that keeps its input order among equal x.
// The passes share the histogram buffer, the histogram prefix sum passes, and the dispatch preparation of the uint radix
// sort. A uint2 key buffer is a uint2 counted buffer whose header holds the same words as a uint radix sort key buffer.

// The number of invocations of the histogram and scatter passes.
#define WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE 64
// The number of elements the scatter pass sorts in groupshared memory at once.
#define WBE_RADIX_SORT_UINT2_BATCH_SIZE 256
// The scatter pass sorts a batch by entries packing the digit of an element above its index in the batch.
#define WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE 8
#define WBE_RADIX_SORT_UINT2_ENTRY_INDEX_MASK ((1u << WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE) - 1)

/**
 * @brief Get the WBE_RADIX_SORT_BUCKET_BIT_SIZE-bit digit of a uint2 key.
 * @param p_key The key, ordered as the 64-bit value (x << 32) | y.
 * @param p_bit_region The bit offset of the digit in the 64-bit value; a multiple of WBE_RADIX_SORT_BUCKET_BIT_SIZE in
 * [0, 64), so that the digit lies within one component.
 * @return The digit, in [0, WBE_RADIX_SORT_BUCKET_COUNT).
 */
uint radix_sort_digit(uint2 p_key, uint p_bit_region) {
    uint word = p_bit_region < 32 ? p_key.y : p_key.x;
    return (word >> (p_bit_region % 32)) & WBE_RADIX_SORT_BUCKET_MASK;
}

/**
 * @brief Writes the digit counts of the keys of one local sort to its sub-histogram, as one histogram pass workgroup of
 * the uint2 radix sort.
 * @details Workgroup p_group_id owns the contiguous key range [g * elements_per_group, (g + 1) * elements_per_group), with
 * elements_per_group = radix_sort_elements_per_local_sort(element_count, local_sort_num), and writes the raw digit counts
 * of that range to sub-histogram g at WBE_RADIX_SORT_LANE_START + g * WBE_RADIX_SORT_BUCKET_COUNT. Workgroup 0 also writes
 * the histogram buffer metadata: the sub-histogram count, equal to the local sort count, the start of the overflow
 * histograms, after the last sub-histogram, and WBE_RADIX_SORT_LANE_START. Workgroups at or past the local sort count do
 * nothing. Every invocation of the WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE-invocation workgroup must call this function with
 * the same arguments except p_group_thread_id.
 * @param p_keys The uint2 key buffer.
 * @param p_histogram_buffer The histogram buffer receiving the sub-histogram and the metadata.
 * @param p_histogram Groupshared scratch histogram; its contents are overwritten.
 * @param p_group_id The workgroup index, which is the local sort index.
 * @param p_group_thread_id The local invocation index in [0, WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE).
 * @param p_bit_region The bit offset of the digit to count, as for radix_sort_digit(uint2, uint).
 */
void radix_sort_uint2_histogram(CountedBuffer<uint2> p_keys, RWStructuredBuffer<uint> p_histogram_buffer,
        __ref groupshared uint p_histogram[WBE_RADIX_SORT_BUCKET_COUNT], uint p_group_id, uint p_group_thread_id,
        uint p_bit_region) {
    uint thread_data_start = p_group_thread_id * 4;
    uint element_count = p_keys.size();
    uint sub_histogram_num = p_keys.get_header(WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX);
    if (p_group_id >= sub_histogram_num) {
        return;
    }
    if (p_group_id == 0 && p_group_thread_id == 0) {
        p_histogram_buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX] = sub_histogram_num;
        p_histogram_buffer[WBE_RADIX_SORT_OVERFLOW_START_INDEX] =
            WBE_RADIX_SORT_LANE_START + sub_histogram_num * WBE_RADIX_SORT_BUCKET_COUNT;
        p_histogram_buffer[WBE_RADIX_SORT_LANE_START_INDEX] = WBE_RADIX_SORT_LANE_START;
    }
    [unroll] for (uint i = 0; i < 4; ++i) {
        p_histogram[thread_data_start + i] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint elements_per_group = radix_sort_elements_per_local_sort(element_count, sub_histogram_num);
    uint group_begin = min(p_group_id * elements_per_group, element_count);
    uint group_end = min(group_begin + elements_per_group, element_count);
    // Each invocation counts a contiguous section of the range, of at least one element.
    uint operate_stride =
        max((group_end - group_begin + WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE - 1) / WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE, 1u);
    uint section_begin = group_begin + p_group_thread_id * operate_stride;
    uint section_end = min(section_begin + operate_stride, group_end);
    for (uint i = section_begin; i < section_end; ++i) {
        InterlockedAdd(p_histogram[radix_sort_digit(p_keys.get(i), p_bit_region)], 1);
    }
    GroupMemoryBarrierWithGroupSync();

    uint sub_histogram_start = WBE_RADIX_SORT_LANE_START + p_group_id * WBE_RADIX_SORT_BUCKET_COUNT;
    [unroll] for (uint i = 0; i < 4; ++i) {
        p_histogram_buffer[sub_histogram_start + thread_data_start + i] = p_histogram[thread_data_start + i];
    }
}

/**
 * @brief Scatters the keys of one local sort into their sorted positions for one digit, as one scatter pass workgroup of
 * the uint2 radix sort.
 * @details Workgroup p_group_id owns the key range that sub-histogram g counted in radix_sort_uint2_histogram(). The
 * histogram buffer must hold the column-wise inclusive prefix sum of the sub-histograms, from
 * radix_sort_histogram_add_overflow. Each key is written to: (number of keys with a smaller digit) + (number of keys with
 * the same digit in earlier sub-histograms) + (rank among the keys with the same digit in this sub-histogram). The range is
 * sorted in batches of WBE_RADIX_SORT_UINT2_BATCH_SIZE: each key of a batch is stored in groupshared memory, and an entry
 * packing its digit above its index in the batch is sorted stably by the digit, so the scatter is stable. Workgroup 0 also
 * copies the element count and the local sort count of the input keys to the output keys. Workgroups at or past the
 * sub-histogram count do nothing. Every invocation of the WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE-invocation workgroup must
 * call this function with the same arguments except p_group_thread_id.
 * @param p_input_keys The uint2 key buffer read.
 * @param p_histogram_buffer The prefix-summed histogram buffer.
 * @param p_output_keys The uint2 key buffer receiving the scattered keys, the element count, and the local sort count.
 * @param p_entries Groupshared scratch for the sorted batch entries; its contents are overwritten.
 * @param p_scratch Groupshared scratch for the batch sort; its contents are overwritten.
 * @param p_batch_keys Groupshared scratch for the keys of a batch; its contents are overwritten.
 * @param p_digit_offsets Groupshared scratch for the output index of the next key of each digit; its contents are
 * overwritten.
 * @param p_batch_digit_starts Groupshared scratch for the first entry of each digit in a sorted batch; its contents are
 * overwritten.
 * @param p_group_id The workgroup index, which is the sub-histogram index.
 * @param p_group_thread_id The local invocation index in [0, WBE_RADIX_SORT_UINT2_WORKGROUP_SIZE).
 * @param p_bit_region The bit offset of the digit to sort by, as for radix_sort_digit(uint2, uint).
 */
void radix_sort_uint2_scatter(CountedBuffer<uint2> p_input_keys, RWStructuredBuffer<uint> p_histogram_buffer,
        CountedBuffer<uint2> p_output_keys, __ref groupshared uint p_entries[WBE_RADIX_SORT_UINT2_BATCH_SIZE],
        __ref groupshared uint p_scratch[WBE_RADIX_SORT_UINT2_BATCH_SIZE],
        __ref groupshared uint2 p_batch_keys[WBE_RADIX_SORT_UINT2_BATCH_SIZE],
        __ref groupshared uint p_digit_offsets[WBE_RADIX_SORT_BUCKET_COUNT],
        __ref groupshared uint p_batch_digit_starts[WBE_RADIX_SORT_BUCKET_COUNT], uint p_group_id, uint p_group_thread_id,
        uint p_bit_region) {
    uint thread_data_start = p_group_thread_id * 4;
    uint element_count = p_input_keys.size();
    uint sub_histogram_num = p_histogram_buffer[WBE_RADIX_SORT_SUB_HISTOGRAM_NUM_INDEX];
    uint lane_start = p_histogram_buffer[WBE_RADIX_SORT_LANE_START_INDEX];
    if (p_group_id >= sub_histogram_num) {
        return;
    }
    if (p_group_id == 0 && p_group_thread_id == 0) {
        p_output_keys.set_size(element_count);
        p_output_keys.set_header(
            WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX, p_input_keys.get_header(WBE_RADIX_SORT_LOCAL_SORT_NUM_INDEX));
    }

    // The last sub-histogram holds the total count of each digit after the column-wise inclusive prefix sum. An exclusive
    // prefix sum over the digits gives the output index of the first key of each digit.
    uint last_sub_histogram_start = lane_start + (sub_histogram_num - 1) * WBE_RADIX_SORT_BUCKET_COUNT;
    [unroll] for (uint i = 0; i < 4; ++i) {
        p_scratch[thread_data_start + i] = p_histogram_buffer[last_sub_histogram_start + thread_data_start + i];
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_prefix_sum_64_256<uint, Add<uint>>(p_scratch, thread_data_start);
    [unroll] for (uint i = 0; i < 4; ++i) {
        uint digit = thread_data_start + i;
        // The index of the first digit underflows past the end, so it reads zero.
        uint previous_digit = digit - 1u;
        uint digit_start = previous_digit < WBE_RADIX_SORT_BUCKET_COUNT ? p_scratch[previous_digit] : 0;
        // The inclusive prefix sum of the previous sub-histogram counts this digit in all earlier sub-histograms.
        uint earlier_sub_histogram_count = 0;
        if (p_group_id > 0) {
            earlier_sub_histogram_count =
                p_histogram_buffer[lane_start + (p_group_id - 1) * WBE_RADIX_SORT_BUCKET_COUNT + digit];
        }
        p_digit_offsets[digit] = digit_start + earlier_sub_histogram_count;
    }

    uint elements_per_group = radix_sort_elements_per_local_sort(element_count, sub_histogram_num);
    uint group_begin = min(p_group_id * elements_per_group, element_count);
    uint group_end = min(group_begin + elements_per_group, element_count);
    for (uint batch_begin = group_begin; batch_begin < group_end; batch_begin += WBE_RADIX_SORT_UINT2_BATCH_SIZE) {
        uint batch_count = min(group_end - batch_begin, WBE_RADIX_SORT_UINT2_BATCH_SIZE);
        // Padding entries have the largest digit and the largest indices, so the stable sort keeps them after every key.
        [unroll] for (uint i = 0; i < 4; ++i) {
            uint index = thread_data_start + i;
            uint digit = WBE_RADIX_SORT_BUCKET_MASK;
            if (index < batch_count) {
                uint2 key = p_input_keys.get(batch_begin + index);
                p_batch_keys[index] = key;
                digit = radix_sort_digit(key, p_bit_region);
            }
            p_entries[index] = (digit << WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE) | index;
        }
        // Also orders the p_scratch reads above before the sort overwrites it.
        GroupMemoryBarrierWithGroupSync();
        // Stably sort the entries by the digit above the index only.
        [unroll] for (uint i = 0; i < WBE_RADIX_SORT_BUCKET_BIT_SIZE / WBE_RADIX_SORT_WG_BIT_SIZE; ++i) {
            workgroup_radix_sort_step_64_256(p_entries,
                WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE + i * WBE_RADIX_SORT_WG_BIT_SIZE, p_group_thread_id, p_scratch);
        }

        uint4 sources;
        uint4 digits;
        [unroll] for (uint i = 0; i < 4; ++i) {
            uint index = thread_data_start + i;
            uint entry = p_entries[index];
            sources[i] = entry & WBE_RADIX_SORT_UINT2_ENTRY_INDEX_MASK;
            digits[i] = entry >> WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE;
            if (index < batch_count
                && (index == 0 || (p_entries[index - 1] >> WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE) != digits[i])) {
                p_batch_digit_starts[digits[i]] = index;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // The number of keys of a digit in the batch, held by the invocation owning the last entry of that digit.
        uint4 batch_digit_counts = uint4(0);
        [unroll] for (uint i = 0; i < 4; ++i) {
            uint index = thread_data_start + i;
            if (index < batch_count) {
                uint rank = index - p_batch_digit_starts[digits[i]];
                p_output_keys.set(p_digit_offsets[digits[i]] + rank, p_batch_keys[sources[i]]);
                if (index == batch_count - 1
                    || (p_entries[index + 1] >> WBE_RADIX_SORT_UINT2_ENTRY_INDEX_BIT_SIZE) != digits[i]) {
                    batch_digit_counts[i] = rank + 1;
                }
            }
        }
        // Every invocation has read p_digit_offsets before it is advanced.
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint i = 0; i < 4; ++i) {
            if (batch_digit_counts[i] > 0) {
                p_digit_offsets[digits[i]] += batch_digit_counts[i];
            }
        }
        // The next batch overwrites p_entries and p_batch_keys and reads p_digit_offsets.
        GroupMemoryBarrierWithGroupSync();
    }
}

#endif

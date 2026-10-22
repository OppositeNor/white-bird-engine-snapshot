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

#ifndef WBE_FILE_PREFIX_SUM_UTILS_SLANG
#define WBE_FILE_PREFIX_SUM_UTILS_SLANG
/**
 * @brief Computes an in-place inclusive prefix sum over a 256-element groupshared array with a 64-invocation workgroup.
 * @details Uses a Hillis-Steele scan in which each invocation owns 4 contiguous elements, starting at p_start, and
 * updates them as one uint4 per step. The 64 invocations must together own every element of p_data[0, 256) exactly
 * once. Reads that would fall before the start of the array, where the unsigned index underflows, read zero instead.
 * Every invocation of the 64-invocation workgroup must call this function with the same array. The
 * function does not synchronize before its first read, so callers must issue a workgroup barrier after filling the
 * array; it synchronizes after the final write, so the results are visible to the whole workgroup on return.
 * @param p_data The groupshared array to sum in place.
 * @param p_start The index of the first of the 4 contiguous elements this invocation owns; a multiple of 4 in
 * [0, 256).
 */
void workgroup_inclusive_prefix_sum_64_256(__ref groupshared uint p_data[256], uint p_start) {
    [unroll] for (uint i = 0; i < 8; ++i) {
        // We split the 256 entries in chunks of 4, where each thread performs SIMD on the 4 entries they're responsible of.
        uint offset = 1 << i;
        // An index before the start of the array underflows past the end, so it reads zero.
        uint4 left_region_data;
        [unroll] for (uint k = 0; k < 4; ++k) {
            uint left_index = p_start - offset + k;
            left_region_data[k] = left_index < 256 ? p_data[left_index] : 0;
        }
        uint4 curr_region_data = uint4(
            p_data[p_start + 0],
            p_data[p_start + 1],
            p_data[p_start + 2],
            p_data[p_start + 3]
        );
        curr_region_data += left_region_data;
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint i = 0; i < 4; ++i) {
            p_data[p_start + i] = curr_region_data[i];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

/**
 * @brief Computes an in-place inclusive prefix sum over a buffer range of any length with one workgroup.
 * @details Sums p_buffer[p_start, p_start + p_element_count) in chunks of 4 * N elements, from the first chunk to the
 * last. In each chunk, invocation p_thread_id loads the 4 contiguous elements starting at 4 * p_thread_id and sums them
 * locally, the invocation totals are scanned in p_scratch with a Hillis-Steele scan, and every invocation stores its
 * elements plus the totals of the earlier invocations and the sum of every earlier chunk. Each element of p_buffer is
 * only accessed by the invocation that owns it, so the invocations exchange values through p_scratch alone: buffer
 * accesses need no shader-level memory synchronization. Elements outside the range are neither read nor written. Every
 * invocation of the N-invocation workgroup must call this function with the same buffer, start, element count, and
 * scratch array, and only one workgroup may operate on the range at a time. The function synchronizes before reusing
 * p_scratch for the next chunk; each invocation's results are written by that invocation, so callers must order any read
 * of elements written by other invocations, for example with a later dispatch.
 * @todo Slang does not support globallycoherent descriptors fetched from the descriptor heap
 * (https://github.com/shader-slang/slang/issues/13440), so under the Vulkan memory model, accesses to p_buffer are private
 * and barriers do not make them visible to other invocations. The invocations therefore never exchange values through
 * p_buffer.
 * @tparam N The number of invocations in the workgroup.
 * @param p_buffer The buffer whose range is summed in place.
 * @param p_start The index of the first element of the range.
 * @param p_element_count The number of elements in the range.
 * @param p_thread_id The local invocation index in [0, N).
 * @param p_scratch Scratch array of one element per invocation; its contents are overwritten.
 * @return The sum of every element in the range, the same in every invocation.
 */
uint workgroup_general_prefix_sum<let N : uint>(RWStructuredBuffer<uint> p_buffer, uint p_start, uint p_element_count,
        uint p_thread_id, __ref groupshared uint p_scratch[N]) {
    // The sum of every element in the earlier chunks.
    uint carry = 0;
    for (uint chunk_start = 0; chunk_start < p_element_count; chunk_start += 4 * N) {
        uint element_start = chunk_start + p_thread_id * 4;
        // Elements past the range count as zero, so they do not change the sums.
        uint4 values = uint4(0);
        [unroll] for (uint k = 0; k < 4; ++k) {
            if (element_start + k < p_element_count) {
                values[k] = p_buffer[p_start + element_start + k];
            }
        }
        values.y += values.x;
        values.z += values.y;
        values.w += values.z;
        p_scratch[p_thread_id] = values.w;
        GroupMemoryBarrierWithGroupSync();
        // Inclusive scan of the invocation totals.
        for (uint offset = 1; offset < N; offset <<= 1) {
            uint left = p_thread_id >= offset ? p_scratch[p_thread_id - offset] : 0;
            GroupMemoryBarrierWithGroupSync();
            p_scratch[p_thread_id] += left;
            GroupMemoryBarrierWithGroupSync();
        }
        uint thread_offset = carry + (p_thread_id == 0 ? 0 : p_scratch[p_thread_id - 1]);
        // The last chunk may extend past the range, which must not be written.
        [unroll] for (uint k = 0; k < 4; ++k) {
            if (element_start + k < p_element_count) {
                p_buffer[p_start + element_start + k] = values[k] + thread_offset;
            }
        }
        carry += p_scratch[N - 1];
        // Every invocation has read p_scratch before the next chunk overwrites it.
        GroupMemoryBarrierWithGroupSync();
    }
    return carry;
}
#endif

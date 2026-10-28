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
 * @brief An associative binary operator that a prefix scan combines elements with.
 * @details The operator need not be commutative: scans always pass the earlier element first.
 * @tparam T The element type.
 */
interface IOperator<T> {
    /**
     * @brief Get the identity element of the operator.
     * @details Scans use it as the combination of no elements, always as the earlier element, so for every x,
     * apply(identity(), x) must equal x. Scans never pass it as the later element, so apply(x, identity()) may differ
     * from x.
     * @return The identity element.
     */
    static T identity();

    /**
     * @brief Combine two elements.
     * @param p_val1 The earlier element.
     * @param p_val2 The later element.
     * @return The combination of p_val1 followed by p_val2.
     */
    static T apply(T p_val1, T p_val2);
}

/**
 * @brief Adds elements, making a prefix scan a prefix sum.
 * @tparam T The element type.
 */
struct Add<T : IArithmetic> : IOperator<T> {
    static T identity() {
        return T(0);
    }

    static T apply(T p_val1, T p_val2) {
        return p_val1 + p_val2;
    }
}

/**
 * @brief An element of a segmented prefix scan: a value and a flag marking where a segment starts.
 * @tparam T The value type.
 */
struct SegmentedWrapper<T> {
    /**
     * @brief The value combined within its segment.
     */
    T value;
    /**
     * @brief Nonzero when a segment starts at this element.
     * @details In a scan result, nonzero when a segment starts at or before the element in the scanned range, as computed
     * by the operator.
     */
    uint break;
};

/**
 * @brief Combines the values of SegmentedWrapper elements with O within segments, making a prefix scan a segmented scan.
 * @details A segment starts at every element whose break is nonzero, and at the start of the scanned range. An inclusive
 * scan gives each element the combination with O of the values from the start of its segment up to and including the
 * element, so every segment is scanned independently, and a break of 1 when a segment starts at or before the element in
 * the range, 0 otherwise. The operator is associative whenever O is: combining an earlier part with a later part that
 * contains a break keeps the later part's value, and otherwise combines both values.
 * @tparam T The value type.
 * @tparam O The operator the values are combined with within a segment.
 */
struct SegmentedPrefix<T, O : IOperator<T>> : IOperator<SegmentedWrapper<T>> {
    static SegmentedWrapper<T> identity() {
        return { O::identity(), 0 };
    }
    static SegmentedWrapper<T> apply(SegmentedWrapper<T> p_val1, SegmentedWrapper<T> p_val2) {
        return {
            p_val2.break == 0 ? O::apply(p_val1.value, p_val2.value) : p_val2.value,
            p_val1.break > 0 || p_val2.break > 0 ? 1 : 0,
        };
    }
}

/**
 * @brief Sums the breaks of SegmentedWrapper elements and keeps their values, making a prefix scan number the segments.
 * @details An inclusive scan leaves every value unchanged and gives each element the sum of the breaks from the start of
 * the scanned range up to and including the element. With breaks of 0 or 1, that is the number of segments starting at
 * or before the element, so the elements of each segment share one count: the segment's index, plus one when a segment
 * starts at the first element. The breaks are summed with wrap-around, so nonzero breaks other than 1 add their value
 * rather than counting once. The operator is associative: combining keeps the later value and adds the breaks.
 * @tparam T The value type.
 * @tparam O The operator whose identity is the value of identity(); it does not combine values.
 */
struct SegmentedBreakPrefix<T, O : IOperator<T>> : IOperator<SegmentedWrapper<T>> {
    static SegmentedWrapper<T> identity() {
        return { O::identity(), 0 };
    }
    static SegmentedWrapper<T> apply(SegmentedWrapper<T> p_val1, SegmentedWrapper<T> p_val2) {
        return {
            p_val2.value,
            p_val1.break + p_val2.break,
        };
    }
}

/**
 * @brief Computes an in-place inclusive prefix scan over a 256-element groupshared array with a 64-invocation workgroup.
 * @details Uses a Hillis-Steele scan in which each invocation owns 4 contiguous elements, starting at p_start, and
 * updates them together each step. The 64 invocations must together own every element of p_data[0, 256) exactly once.
 * Reads that would fall before the start of the array, where the unsigned index underflows, are skipped. Every
 * invocation of the 64-invocation workgroup must call this function with the same array. The function does not
 * synchronize before its first read, so callers must issue a workgroup barrier after filling the array; it synchronizes
 * after the final write, so the results are visible to the whole workgroup on return.
 * @tparam T The element type.
 * @tparam O The operator the elements are combined with.
 * @param p_data The groupshared array to scan in place.
 * @param p_start The index of the first of the 4 contiguous elements this invocation owns; a multiple of 4 in
 * [0, 256).
 */
void workgroup_inclusive_prefix_sum_64_256<T, O : IOperator<T>>(__ref groupshared T p_data[256], uint p_start) {
    [unroll] for (uint i = 0; i < 8; ++i) {
        // We split the 256 entries in chunks of 4, where each thread updates the 4 entries they're responsible of.
        uint offset = 1 << i;
        T values[4];
        [unroll] for (uint k = 0; k < 4; ++k) {
            values[k] = p_data[p_start + k];
            // An index before the start of the array underflows past the end, so it is skipped.
            uint left_index = p_start - offset + k;
            if (left_index < 256) {
                values[k] = O::apply(p_data[left_index], values[k]);
            }
        }
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint k = 0; k < 4; ++k) {
            p_data[p_start + k] = values[k];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

/**
 * @brief Computes an in-place inclusive prefix scan over a buffer range of any length with one workgroup.
 * @details Scans p_buffer[p_start, p_start + p_element_count) in chunks of 4 * N elements, from the first chunk to the
 * last. In each chunk, invocation p_thread_id loads the 4 contiguous elements starting at 4 * p_thread_id and scans them
 * locally, the invocation totals are scanned in p_scratch with a Hillis-Steele scan, and every invocation stores its
 * elements combined after the totals of the earlier invocations and the total of every earlier chunk. Each element of
 * p_buffer is only accessed by the invocation that owns it, so the invocations exchange values through p_scratch alone:
 * buffer accesses need no shader-level memory synchronization. Elements outside the range are neither read nor written.
 * Every invocation of the N-invocation workgroup must call this function with the same buffer, start, element count, and
 * scratch array, and only one workgroup may operate on the range at a time. The function synchronizes before reusing
 * p_scratch for the next chunk; each invocation's results are written by that invocation, so callers must order any read
 * of elements written by other invocations, for example with a later dispatch.
 * @todo Slang does not support globallycoherent descriptors fetched from the descriptor heap
 * (https://github.com/shader-slang/slang/issues/13440), so under the Vulkan memory model, accesses to p_buffer are private
 * and barriers do not make them visible to other invocations. The invocations therefore never exchange values through
 * p_buffer.
 * @tparam T The element type.
 * @tparam O The operator the elements are combined with.
 * @tparam N The number of invocations in the workgroup.
 * @param p_buffer The buffer whose range is scanned in place.
 * @param p_start The index of the first element of the range.
 * @param p_element_count The number of elements in the range.
 * @param p_thread_id The local invocation index in [0, N).
 * @param p_scratch Scratch array of one element per invocation; its contents are overwritten.
 * @return The combination of every element in the range, O::identity() for an empty range, the same in every
 * invocation.
 */
T workgroup_general_prefix_sum<T, O : IOperator<T>, let N : uint>(RWStructuredBuffer<T> p_buffer, uint p_start,
        uint p_element_count, uint p_thread_id, __ref groupshared T p_scratch[N]) {
    // The combination of every element in the earlier chunks.
    T carry = O::identity();
    for (uint chunk_start = 0; chunk_start < p_element_count; chunk_start += 4 * N) {
        uint element_start = chunk_start + p_thread_id * 4;
        // The number of this invocation's elements inside the range. Elements past the range are ignored, so the
        // identity is never combined as the later element.
        uint valid_count = element_start < p_element_count ? min(p_element_count - element_start, 4u) : 0;
        T values[4];
        [unroll] for (uint k = 0; k < 4; ++k) {
            values[k] = k < valid_count ? p_buffer[p_start + element_start + k] : O::identity();
        }
        [unroll] for (uint k = 1; k < 4; ++k) {
            if (k < valid_count) {
                values[k] = O::apply(values[k - 1], values[k]);
            }
        }
        // Invocations past the range hold the identity. Their scanned totals are never read: invocations inside the
        // range only combine the totals of earlier invocations, which are inside the range too.
        p_scratch[p_thread_id] = valid_count > 0 ? values[valid_count - 1] : O::identity();
        GroupMemoryBarrierWithGroupSync();
        // Inclusive scan of the invocation totals.
        for (uint offset = 1; offset < N; offset <<= 1) {
            T current = p_scratch[p_thread_id];
            if (p_thread_id >= offset) {
                current = O::apply(p_scratch[p_thread_id - offset], current);
            }
            GroupMemoryBarrierWithGroupSync();
            p_scratch[p_thread_id] = current;
            GroupMemoryBarrierWithGroupSync();
        }
        T thread_offset = p_thread_id == 0 ? carry : O::apply(carry, p_scratch[p_thread_id - 1]);
        // The last chunk may extend past the range, which must not be written.
        [unroll] for (uint k = 0; k < 4; ++k) {
            if (element_start + k < p_element_count) {
                p_buffer[p_start + element_start + k] = O::apply(thread_offset, values[k]);
            }
        }
        // The last invocation with elements inside the range holds the total of the chunk.
        uint last_thread_id = min((p_element_count - chunk_start - 1) / 4, N - 1);
        carry = O::apply(carry, p_scratch[last_thread_id]);
        // Every invocation has read p_scratch before the next chunk overwrites it.
        GroupMemoryBarrierWithGroupSync();
    }
    return carry;
}
#endif

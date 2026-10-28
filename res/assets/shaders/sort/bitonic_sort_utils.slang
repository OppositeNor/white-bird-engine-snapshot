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
#ifndef WBE_FILE_BITONIC_SORT_UTILS_SLANG
#define WBE_FILE_BITONIC_SORT_UTILS_SLANG

/**
 * @brief A key that the bitonic sort can order.
 */
interface IBitonicSortKey {
    /**
     * @brief Test whether this key orders after another key.
     * @param p_other The key compared against.
     * @return Whether this key is strictly greater than p_other.
     */
    bool is_greater_than(This p_other);
}

/**
 * @brief Orders uint keys by value.
 */
extension uint : IBitonicSortKey {
    bool is_greater_than(uint p_other) {
        return this > p_other;
    }
}

/**
 * @brief Orders uint2 keys lexicographically, by x and then by y, i.e. as the 64-bit value (x << 32) | y.
 */
extension uint2 : IBitonicSortKey {
    bool is_greater_than(uint2 p_other) {
        return this.x != p_other.x ? this.x > p_other.x : this.y > p_other.y;
    }
}

/**
 * @brief Orders two keys in the direction of a bitonic merge step.
 * @tparam T The key type.
 * @param p_low The key at the lower index; receives the smaller key when p_ascending is true.
 * @param p_high The key at the higher index; receives the larger key when p_ascending is true.
 * @param p_ascending Whether the pair is ordered ascending.
 */
void bitonic_compare_exchange<T : IBitonicSortKey>(inout T p_low, inout T p_high, bool p_ascending) {
    if (p_low.is_greater_than(p_high) == p_ascending) {
        T swap = p_low;
        p_low = p_high;
        p_high = swap;
    }
}

/**
 * @brief Sorts a groupshared array in place in ascending order with a workgroup of ELEMENT_COUNT / 4 invocations, each
 * responsible for 4 elements.
 * @details Merge steps whose stride is at least 4 exchange pairs through groupshared memory, two pairs per invocation;
 * the last two steps of each merge run in registers over the invocation's 4 contiguous elements. The sort is not stable.
 * Every invocation of the workgroup must call this function with uniform control flow, after p_data has been written and
 * made visible to the workgroup. On return, the sorted array is visible to the whole workgroup.
 * @tparam T The key type.
 * @tparam ELEMENT_COUNT The number of elements; a power of two of at least 4.
 * @param p_data The groupshared array to sort in place.
 * @param p_thread_id The local invocation index in [0, ELEMENT_COUNT / 4).
 */
void workgroup_bitonic_sort<T : IBitonicSortKey, let ELEMENT_COUNT : uint>(
    __ref groupshared T p_data[ELEMENT_COUNT], uint p_thread_id) {
    const uint workgroup_size = ELEMENT_COUNT / 4;
    const uint data_start = p_thread_id * 4;
    [unroll] for (uint block_size = 2; block_size <= ELEMENT_COUNT; block_size <<= 1) {
        [unroll] for (uint stride = block_size >> 1; stride >= 4; stride >>= 1) {
            [unroll] for (uint i = 0; i < 2; ++i) {
                uint pair = p_thread_id + i * workgroup_size;
                uint low = (pair / stride) * stride * 2 + pair % stride;
                uint high = low + stride;
                T low_key = p_data[low];
                T high_key = p_data[high];
                bitonic_compare_exchange(low_key, high_key, (low & block_size) == 0);
                p_data[low] = low_key;
                p_data[high] = high_key;
            }
            GroupMemoryBarrierWithGroupSync();
        }

        T keys[4];
        [unroll] for (uint i = 0; i < 4; ++i) {
            keys[i] = p_data[data_start + i];
        }
        if (block_size >= 4) {
            // A block of at least 4 elements contains all 4 elements, so they share the direction.
            bool ascending = (data_start & block_size) == 0;
            bitonic_compare_exchange(keys[0], keys[2], ascending);
            bitonic_compare_exchange(keys[1], keys[3], ascending);
        }
        bitonic_compare_exchange(keys[0], keys[1], (data_start & block_size) == 0);
        bitonic_compare_exchange(keys[2], keys[3], ((data_start + 2) & block_size) == 0);
        [unroll] for (uint i = 0; i < 4; ++i) {
            p_data[data_start + i] = keys[i];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

/**
 * @brief Sorts a 256-element groupshared array of uint keys in place in ascending order with a 64-invocation workgroup.
 * @details As workgroup_bitonic_sort().
 * @param p_data The groupshared array to sort in place.
 * @param p_thread_id The local invocation index in [0, 64).
 */
void workgroup_bitonic_sort_64_256(__ref groupshared uint p_data[256], uint p_thread_id) {
    workgroup_bitonic_sort<uint, 256>(p_data, p_thread_id);
}

/**
 * @brief Sorts a 1024-element groupshared array of uint keys in place in ascending order with a 256-invocation workgroup.
 * @details As workgroup_bitonic_sort().
 * @param p_data The groupshared array to sort in place.
 * @param p_thread_id The local invocation index in [0, 256).
 */
void workgroup_bitonic_sort_256_1024(__ref groupshared uint p_data[1024], uint p_thread_id) {
    workgroup_bitonic_sort<uint, 1024>(p_data, p_thread_id);
}

/**
 * @brief Sorts a 1024-element groupshared array of uint2 keys in place in ascending lexicographic order, by x and then by
 * y, with a 256-invocation workgroup.
 * @details As workgroup_bitonic_sort().
 * @param p_data The groupshared array to sort in place.
 * @param p_thread_id The local invocation index in [0, 256).
 */
void workgroup_bitonic_sort_256_1024(__ref groupshared uint2 p_data[1024], uint p_thread_id) {
    workgroup_bitonic_sort<uint2, 1024>(p_data, p_thread_id);
}

#endif

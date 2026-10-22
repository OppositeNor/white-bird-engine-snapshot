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
 * @brief Orders two keys in the direction of a bitonic merge step.
 * @param p_low The key at the lower index; receives the smaller key when p_ascending is true.
 * @param p_high The key at the higher index; receives the larger key when p_ascending is true.
 * @param p_ascending Whether the pair is ordered ascending.
 */
void bitonic_compare_exchange(inout uint p_low, inout uint p_high, bool p_ascending) {
    if ((p_low > p_high) == p_ascending) {
        uint swap = p_low;
        p_low = p_high;
        p_high = swap;
    }
}

/**
 * @brief Sorts a 256-element groupshared array of uint keys in place in ascending order with a 64-invocation workgroup.
 * 
 * @param p_data The groupshared array to sort in place.
 * @param p_thread_id The local invocation index in [0, 64).
 */
void workgroup_bitonic_sort_64_256(__ref groupshared uint p_data[256], uint p_thread_id) {
    const uint data_start = p_thread_id * 4;
    [unroll] for (uint block_size = 2; block_size <= 256; block_size <<= 1) {
        [unroll] for (uint stride = block_size >> 1; stride >= 4; stride >>= 1) {
            [unroll] for (uint i = 0; i < 2; ++i) {
                uint pair = p_thread_id + i * 64;
                uint low = (pair / stride) * stride * 2 + pair % stride;
                uint high = low + stride;
                uint low_key = p_data[low];
                uint high_key = p_data[high];
                bitonic_compare_exchange(low_key, high_key, (low & block_size) == 0);
                p_data[low] = low_key;
                p_data[high] = high_key;
            }
            GroupMemoryBarrierWithGroupSync();
        }

        uint4 keys;
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

#endif

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

#ifndef WBE_FILE_DEDUP_LARGE_UTILS_SLANG
#define WBE_FILE_DEDUP_LARGE_UTILS_SLANG

#include "sort/radix_sort_utils.slang"
#include "prefix_sum/large_prefix_scan_utils.slang"

// The large deduplication removes the repeated keys of a key buffer in four steps: the radix sort orders the keys,
// dedup_large_mark() marks the first key of every run of equal keys, the large prefix scan turns the marks into each
// marked key's rank among the unique keys plus one, and dedup_large_scatter() writes each marked key to its rank. The key
// buffer and the unique keys have the layout of a radix sort key buffer, and the marks the layout of a large prefix scan
// buffer.

// The number of invocations of the per-key passes, each of which processes one key.
#define WBE_DEDUP_LARGE_WORKGROUP_SIZE 256

/**
 * @brief Get the number of workgroups of the per-key passes.
 * @param p_key_count The number of keys.
 * @return ceil(p_key_count / WBE_DEDUP_LARGE_WORKGROUP_SIZE).
 */
uint dedup_large_group_num(uint p_key_count) {
    return (p_key_count + WBE_DEDUP_LARGE_WORKGROUP_SIZE - 1) / WBE_DEDUP_LARGE_WORKGROUP_SIZE;
}

/**
 * @brief Marks key p_key of the sorted keys with 1 if it differs from the previous key, or if it is the first key, and
 * with 0 otherwise.
 * @details Key 0 also writes the key count to the marks. Keys at or past the key count are ignored.
 * @param p_keys The keys, sorted.
 * @param p_marks The marks, one per key.
 * @param p_key The key to mark.
 */
void dedup_large_mark(RWStructuredBuffer<uint> p_keys, RWStructuredBuffer<uint> p_marks, uint p_key) {
    uint key_count = p_keys[WBE_RADIX_SORT_ELEMENT_COUNT_INDEX];
    if (p_key >= key_count) {
        return;
    }
    if (p_key == 0) {
        p_marks[WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX] = key_count;
    }
    bool unique = p_key == 0 || p_keys[WBE_RADIX_SORT_DATA_START + p_key] != p_keys[WBE_RADIX_SORT_DATA_START + p_key - 1];
    p_marks[WBE_LARGE_PREFIX_SCAN_DATA_START + p_key] = unique ? 1 : 0;
}

/**
 * @brief Writes key p_key of the sorted keys to the unique keys if it is the first of its value.
 * @details The inclusive prefix sum of the marks gives each marked key its rank among the unique keys plus one, and its
 * last entry the number of unique keys, which key 0 writes as the count of the unique keys. The unique keys are in
 * ascending order. Keys at or past the key count are ignored.
 * @param p_keys The keys, sorted.
 * @param p_mark_offsets The inclusive prefix sum of the marks of dedup_large_mark().
 * @param p_unique_keys The unique keys.
 * @param p_key The key to write.
 */
void dedup_large_scatter(RWStructuredBuffer<uint> p_keys, RWStructuredBuffer<uint> p_mark_offsets,
        RWStructuredBuffer<uint> p_unique_keys, uint p_key) {
    uint key_count = p_keys[WBE_RADIX_SORT_ELEMENT_COUNT_INDEX];
    if (p_key >= key_count) {
        return;
    }
    if (p_key == 0) {
        p_unique_keys[WBE_RADIX_SORT_ELEMENT_COUNT_INDEX] = p_mark_offsets[WBE_LARGE_PREFIX_SCAN_DATA_START + key_count - 1];
    }
    uint key = p_keys[WBE_RADIX_SORT_DATA_START + p_key];
    if (p_key == 0 || key != p_keys[WBE_RADIX_SORT_DATA_START + p_key - 1]) {
        p_unique_keys[WBE_RADIX_SORT_DATA_START + p_mark_offsets[WBE_LARGE_PREFIX_SCAN_DATA_START + p_key] - 1] = key;
    }
}

#endif

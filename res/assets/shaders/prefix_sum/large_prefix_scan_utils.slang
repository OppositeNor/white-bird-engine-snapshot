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

#ifndef WBE_FILE_LARGE_PREFIX_SCAN_UTILS_SLANG
#define WBE_FILE_LARGE_PREFIX_SCAN_UTILS_SLANG

#include "prefix_sum/prefix_sum_utils.slang"

// The large prefix scan computes the in-place inclusive prefix sum of a buffer in four passes:
// 1. large_prefix_scan_blocks: each workgroup scans one block of WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE elements and stores the
//    block total.
// 2. large_prefix_scan_sections: WBE_LARGE_PREFIX_SCAN_SECTION_NUM workgroups each scan one contiguous section of the
//    block totals and store the section total.
// 3. large_prefix_scan_totals: one workgroup scans the section totals.
// 4. large_prefix_scan_add: each workgroup adds to its block the sum of every earlier block.
// The scanned buffer holds the element count at WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX and the elements from
// WBE_LARGE_PREFIX_SCAN_DATA_START, the layout of the radix sort key buffers (see radix_sort_utils.slang). The sections
// buffer holds the section totals, then the block totals.

// The number of invocations of the block passes; each owns 4 contiguous elements of a block.
#define WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE 64
#define WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE 256
// The number of sections, which is also the number of section totals scanned by one workgroup.
#define WBE_LARGE_PREFIX_SCAN_SECTION_NUM 256
#define WBE_LARGE_PREFIX_SCAN_DATA_START 4
#define WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX 0
#define WBE_LARGE_PREFIX_SCAN_SECTION_TOTALS_START 0
#define WBE_LARGE_PREFIX_SCAN_BLOCK_TOTALS_START WBE_LARGE_PREFIX_SCAN_SECTION_NUM

/**
 * @brief Get the number of blocks, and so of block-pass workgroups, of a large prefix scan.
 * @param p_element_count The number of elements scanned.
 * @return ceil(p_element_count / WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE).
 */
uint large_prefix_scan_block_num(uint p_element_count) {
    return (p_element_count + WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE - 1) / WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE;
}

/**
 * @brief Get the number of block totals each section of a large prefix scan owns.
 * @details Section s owns the block totals [s * blocks_per_section, (s + 1) * blocks_per_section), clamped to the block
 * count.
 * @param p_block_num The number of blocks, from large_prefix_scan_block_num().
 * @return max(ceil(p_block_num / WBE_LARGE_PREFIX_SCAN_SECTION_NUM), 1).
 */
uint large_prefix_scan_blocks_per_section(uint p_block_num) {
    return max((p_block_num + WBE_LARGE_PREFIX_SCAN_SECTION_NUM - 1) / WBE_LARGE_PREFIX_SCAN_SECTION_NUM, 1u);
}

/**
 * @brief First pass of the large prefix scan: scans block p_block in place and stores its total.
 * @details Elements past the element count count as zero and are not written. Block p_block's total is written to
 * p_sections[WBE_LARGE_PREFIX_SCAN_BLOCK_TOTALS_START + p_block]. Every invocation of the
 * WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE-invocation workgroup must call this function with the same arguments, except
 * p_thread_id.
 * @param p_data The scanned buffer.
 * @param p_sections The sections buffer.
 * @param p_block The block of this workgroup, in [0, large_prefix_scan_block_num(element count)).
 * @param p_thread_id The local invocation index in [0, WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE).
 * @param p_scratch Scratch array of one element per block element; its contents are overwritten.
 */
void large_prefix_scan_blocks(RWStructuredBuffer<uint> p_data, RWStructuredBuffer<uint> p_sections, uint p_block,
        uint p_thread_id, __ref groupshared uint p_scratch[WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE]) {
    uint element_count = p_data[WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX];
    uint thread_data_start = p_thread_id * 4;
    uint block_start = p_block * WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE;
    [unroll] for (uint i = 0; i < 4; ++i) {
        uint element = block_start + thread_data_start + i;
        p_scratch[thread_data_start + i] = element < element_count ? p_data[WBE_LARGE_PREFIX_SCAN_DATA_START + element] : 0;
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_prefix_sum_64_256<uint, Add<uint>>(p_scratch, thread_data_start);
    [unroll] for (uint i = 0; i < 4; ++i) {
        uint element = block_start + thread_data_start + i;
        if (element < element_count) {
            p_data[WBE_LARGE_PREFIX_SCAN_DATA_START + element] = p_scratch[thread_data_start + i];
        }
    }
    if (p_thread_id == WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE - 1) {
        p_sections[WBE_LARGE_PREFIX_SCAN_BLOCK_TOTALS_START + p_block] = p_scratch[WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE - 1];
    }
}

/**
 * @brief Second pass of the large prefix scan: scans the block totals of section p_section in place and stores its total.
 * @details Section p_section's total is written to p_sections[WBE_LARGE_PREFIX_SCAN_SECTION_TOTALS_START + p_section]; a
 * section past the last block has a total of zero. Every invocation of the
 * WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE-invocation workgroup must call this function with the same arguments, except
 * p_thread_id.
 * @param p_data The scanned buffer, whose element count is read.
 * @param p_sections The sections buffer, holding the block totals of large_prefix_scan_blocks().
 * @param p_section The section of this workgroup, in [0, WBE_LARGE_PREFIX_SCAN_SECTION_NUM).
 * @param p_thread_id The local invocation index in [0, WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE).
 * @param p_scratch Scratch array of one element per invocation; its contents are overwritten.
 */
void large_prefix_scan_sections(RWStructuredBuffer<uint> p_data, RWStructuredBuffer<uint> p_sections, uint p_section,
        uint p_thread_id, __ref groupshared uint p_scratch[WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE]) {
    uint block_num = large_prefix_scan_block_num(p_data[WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX]);
    uint blocks_per_section = large_prefix_scan_blocks_per_section(block_num);
    uint section_begin = min(p_section * blocks_per_section, block_num);
    uint section_end = min(section_begin + blocks_per_section, block_num);
    uint total = workgroup_general_prefix_sum<uint, Add<uint>, WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE>(p_sections,
        WBE_LARGE_PREFIX_SCAN_BLOCK_TOTALS_START + section_begin, section_end - section_begin, p_thread_id, p_scratch);
    if (p_thread_id == 0) {
        p_sections[WBE_LARGE_PREFIX_SCAN_SECTION_TOTALS_START + p_section] = total;
    }
}

/**
 * @brief Third pass of the large prefix scan: scans the section totals in place.
 * @details Must run on exactly one WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE-invocation workgroup, every invocation of which
 * must call this function with the same arguments, except p_thread_id. The last section total then holds the sum of every
 * element.
 * @param p_sections The sections buffer, holding the section totals of large_prefix_scan_sections().
 * @param p_thread_id The local invocation index in [0, WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE).
 * @param p_scratch Scratch array of one element per section; its contents are overwritten.
 */
void large_prefix_scan_totals(RWStructuredBuffer<uint> p_sections, uint p_thread_id,
        __ref groupshared uint p_scratch[WBE_LARGE_PREFIX_SCAN_SECTION_NUM]) {
    uint thread_data_start = p_thread_id * 4;
    [unroll] for (uint i = 0; i < 4; ++i) {
        p_scratch[thread_data_start + i] = p_sections[WBE_LARGE_PREFIX_SCAN_SECTION_TOTALS_START + thread_data_start + i];
    }
    GroupMemoryBarrierWithGroupSync();
    workgroup_inclusive_prefix_sum_64_256<uint, Add<uint>>(p_scratch, thread_data_start);
    [unroll] for (uint i = 0; i < 4; ++i) {
        p_sections[WBE_LARGE_PREFIX_SCAN_SECTION_TOTALS_START + thread_data_start + i] = p_scratch[thread_data_start + i];
    }
}

/**
 * @brief Last pass of the large prefix scan: adds to block p_block the sum of every earlier block.
 * @details The sum of blocks [0, p_block) is the scanned total of block p_block - 1 within its section plus the scanned
 * total of the sections before it. Elements past the element count are not written. Each invocation accesses only its own
 * 4 elements.
 * @param p_data The scanned buffer, holding the block scans of large_prefix_scan_blocks().
 * @param p_sections The sections buffer, holding the scans of large_prefix_scan_sections() and large_prefix_scan_totals().
 * @param p_block The block of this workgroup, in [0, large_prefix_scan_block_num(element count)).
 * @param p_thread_id The local invocation index in [0, WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE).
 */
void large_prefix_scan_add(RWStructuredBuffer<uint> p_data, RWStructuredBuffer<uint> p_sections, uint p_block,
        uint p_thread_id) {
    if (p_block == 0) {
        return;
    }
    uint element_count = p_data[WBE_LARGE_PREFIX_SCAN_ELEMENT_COUNT_INDEX];
    uint previous_block = p_block - 1;
    uint previous_section = previous_block / large_prefix_scan_blocks_per_section(large_prefix_scan_block_num(element_count));
    uint offset = p_sections[WBE_LARGE_PREFIX_SCAN_BLOCK_TOTALS_START + previous_block];
    if (previous_section > 0) {
        offset += p_sections[WBE_LARGE_PREFIX_SCAN_SECTION_TOTALS_START + previous_section - 1];
    }
    uint element_start = p_block * WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE + p_thread_id * 4;
    [unroll] for (uint i = 0; i < 4; ++i) {
        uint element = element_start + i;
        if (element < element_count) {
            p_data[WBE_LARGE_PREFIX_SCAN_DATA_START + element] += offset;
        }
    }
}

#endif

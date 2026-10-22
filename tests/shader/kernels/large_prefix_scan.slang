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
/**
 * @file large_prefix_scan.slang
 * @brief Test kernel that runs one pass of the large prefix scan from prefix_sum/large_prefix_scan_utils.slang.
 * @details Required define: KERNEL_PASS, 0 for large_prefix_scan_blocks(), 1 for large_prefix_scan_sections(), 2 for
 * large_prefix_scan_totals(), and 3 for large_prefix_scan_add(). Each workgroup runs the pass for its group id.
 */
#include "prefix_sum/large_prefix_scan_utils.slang"

RWStructuredBuffer<uint> data;
RWStructuredBuffer<uint> sections;
#if KERNEL_PASS == 0
groupshared uint scratch[WBE_LARGE_PREFIX_SCAN_BLOCK_SIZE];
#elif KERNEL_PASS == 1
groupshared uint scratch[WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE];
#elif KERNEL_PASS == 2
groupshared uint scratch[WBE_LARGE_PREFIX_SCAN_SECTION_NUM];
#endif

[shader("compute")]
[numthreads(WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
#if KERNEL_PASS == 0
    large_prefix_scan_blocks(data, sections, p_group_id.x, p_group_thread_id.x, scratch);
#elif KERNEL_PASS == 1
    large_prefix_scan_sections(data, sections, p_group_id.x, p_group_thread_id.x, scratch);
#elif KERNEL_PASS == 2
    large_prefix_scan_totals(sections, p_group_thread_id.x, scratch);
#else
    large_prefix_scan_add(data, sections, p_group_id.x, p_group_thread_id.x);
#endif
}

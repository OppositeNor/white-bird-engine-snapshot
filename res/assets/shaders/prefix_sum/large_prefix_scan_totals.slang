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
#include "prefix_sum/large_prefix_scan_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "sections_buffer", "read_write")] uint2 sections_buffer;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

groupshared uint section_totals[WBE_LARGE_PREFIX_SCAN_SECTION_NUM];

// Third pass of the large prefix scan; see large_prefix_scan_totals(). Must be dispatched with exactly one workgroup.
[shader("compute")]
[numthreads(WBE_LARGE_PREFIX_SCAN_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_group_thread_id : SV_GroupThreadID) {
    RWStructuredBuffer<uint> sections_buffer = ResourceDescriptorHeap[descriptor_indices.sections_buffer.x];
    large_prefix_scan_totals(sections_buffer, p_group_thread_id.x, section_totals);
}

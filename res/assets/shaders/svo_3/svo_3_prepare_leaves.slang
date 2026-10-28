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
#include "svo/svo_common.slang"
#include "sort/radix_sort_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "svo_info", "read")] uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list", "read_write")] uint2 deferred_list;
    [WBE_PUSH_CONSTANT("frame_slot")] uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Clamps the leaf count of the deferred list filled by svo_gather_leaves to its capacity, whose overflow
// svo_gather_leaves reports to shader validation, so that the deduplication reads only registered leaves. Must be
// dispatched with exactly one workgroup.
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    RWStructuredBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;
    CountedBuffer<uint> deferred_list =
        CountedBuffer<uint>(descriptor_indices.deferred_list, frame_slot);
    deferred_list.set_size(min(deferred_list.size(), svo_info[0].max_deferred_task_count));
}

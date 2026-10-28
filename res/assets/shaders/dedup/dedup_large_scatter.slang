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
#include "dedup/dedup_large_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "keys", "read")] uint2 keys;
    [WBE_DESCRIPTOR("storage_buffer", "marks", "read")] uint2 marks;
    [WBE_DESCRIPTOR("storage_buffer", "unique_keys", "write")] uint2 unique_keys;
    [WBE_PUSH_CONSTANT("frame_slot")] uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Scatters the first key of every value in the sorted keys to the unique keys; see dedup_large_scatter(). The marks must
// hold their inclusive prefix sum. Must be dispatched with the per-key dispatch.
[shader("compute")]
[numthreads(WBE_DEDUP_LARGE_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;
    CountedBuffer<uint> keys = CountedBuffer<uint>(descriptor_indices.keys, frame_slot);
    RWStructuredBuffer<uint> marks = ResourceDescriptorHeap[descriptor_indices.marks.x];
    CountedBuffer<uint> unique_keys = CountedBuffer<uint>(descriptor_indices.unique_keys, frame_slot);
    dedup_large_scatter(keys, marks, unique_keys, p_dispatch_thread_id.x);
}

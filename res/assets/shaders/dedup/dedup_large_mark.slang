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
    [WBE_DESCRIPTOR("storage_buffer", "marks", "write")] uint2 marks;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Marks the first key of every value in the sorted keys; see dedup_large_mark(). Must be dispatched with the per-key
// dispatch.
[shader("compute")]
[numthreads(WBE_DEDUP_LARGE_WORKGROUP_SIZE, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    RWStructuredBuffer<uint> keys = ResourceDescriptorHeap[descriptor_indices.keys.x];
    RWStructuredBuffer<uint> marks = ResourceDescriptorHeap[descriptor_indices.marks.x];
    dedup_large_mark(keys, marks, p_dispatch_thread_id.x);
}

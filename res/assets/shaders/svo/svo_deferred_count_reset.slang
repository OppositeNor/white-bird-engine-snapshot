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
#include "svo/svo_utils.slang"

struct SVODeferredCountResetDescriptorIndices {
#ifdef WBE_RESET_PING_COUNT
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list_ping", "write")]
    uint2 deferred_list;
#else
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list_pong", "write")]
    uint2 deferred_list;
#endif
};

[vk::push_constant]
ConstantBuffer<SVODeferredCountResetDescriptorIndices> descriptor_indices;

/**
 * @brief Clears the deferred-list count selected by the shader variant.
 */
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    RWStructuredBuffer<uint> deferred_list = ResourceDescriptorHeap[descriptor_indices.deferred_list.x];
    deferred_list[SVO_DEFERRED_LIST_COUNT_INDEX] = 0;
}

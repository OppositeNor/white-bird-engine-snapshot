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

struct DrawIndexedIndirectCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

struct SVODebugDrawSetupDescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "svo_info", "read")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_debug_draw_indirect_buffer")]
    uint2 indirect_draw;
};

[vk::push_constant]
ConstantBuffer<SVODebugDrawSetupDescriptorIndices> descriptor_indices;

/**
 * @brief Builds the indexed-indirect command used to draw every SVO leaf plus the tree's bounds.
 */
[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
    StructuredBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    RWStructuredBuffer<DrawIndexedIndirectCommand> indirect_draw = ResourceDescriptorHeap[descriptor_indices.indirect_draw.x];
    DrawIndexedIndirectCommand command;
    command.index_count = 24;
    command.instance_count = svo_info[0].leaves_count + svo_info[0].dynamic_leaves_count + 1;
    command.first_index = 0;
    command.vertex_offset = 0;
    command.first_instance = 0;
    indirect_draw[0] = command;
}

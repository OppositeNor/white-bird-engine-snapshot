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

struct SVOPreDeferredDebugCaptureDescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "svo_info", "read")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_deferred_list_ping", "read")]
    uint2 deferred_list;
    [WBE_DESCRIPTOR("storage_buffer", "svo_pre_deferred_debug_info_buffer")]
    uint2 captured_svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_pre_deferred_debug_list_buffer")]
    uint2 captured_deferred_list;
    [WBE_DESCRIPTOR("storage_buffer", "svo_pre_deferred_debug_draw_indirect_buffer")]
    uint2 indirect_draw;
};

[[vk::push_constant]]
ConstantBuffer<SVOPreDeferredDebugCaptureDescriptorIndices> descriptor_indices;

/**
 * @brief Captures the SVO state produced by the raster passes before deferred propagation changes it.
 */
[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint3 p_dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    StructuredBuffer<uint> deferred_list = ResourceDescriptorHeap[descriptor_indices.deferred_list.x];
    RWStructuredBuffer<SVOInfo> captured_svo_info = ResourceDescriptorHeap[descriptor_indices.captured_svo_info.x];
    RWStructuredBuffer<uint> captured_deferred_list =
        ResourceDescriptorHeap[descriptor_indices.captured_deferred_list.x];
    RWStructuredBuffer<DrawIndexedIndirectCommand> indirect_draw = ResourceDescriptorHeap[descriptor_indices.indirect_draw.x];

    SVOInfo info = svo_info[0];
    uint deferred_task_count = deferred_list[SVO_DEFERRED_LIST_COUNT_INDEX];
    if (p_dispatch_thread_id.x == 0) {
        captured_svo_info[0] = info;
        DrawIndexedIndirectCommand command;
        command.index_count = 24;
        command.instance_count = info.leaves_count + info.dynamic_leaves_count + deferred_task_count;
        command.first_index = 0;
        command.vertex_offset = 0;
        command.first_instance = 0;
        indirect_draw[0] = command;
    }
    // The list may exceed one dispatch's thread count, so each invocation strides over the whole dispatch.
    uint task_stride = WorkgroupCount().x * 128;
    for (uint task_index = p_dispatch_thread_id.x; task_index < deferred_task_count; task_index += task_stride) {
        captured_deferred_list[task_index] = deferred_list[SVO_DEFERRED_LIST_ENTRY_OFFSET + task_index];
    }
}

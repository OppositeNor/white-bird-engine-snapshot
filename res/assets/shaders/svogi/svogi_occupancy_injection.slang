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
#include "svogi/svogi_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "svo_pre_deferred_debug_info_buffer")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer", "read")]
    uint2 tree_nodes;
    [WBE_DESCRIPTOR("storage_buffer", "svo_leaf_buffer", "read")]
    uint2 tree_leaves;
    [WBE_DESCRIPTOR("storage_buffer", "atlas", "write")]
    uint2 atlas;
    [WBE_PUSH_CONSTANT("frame_slot")]
    uint shader_debug_frame_slot;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint p_group_id : SV_GroupID, uint p_group_thread_id : SV_GroupThreadID, uint p_thread_id : SV_DispatchThreadID) {
    ConstantBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    StructuredBuffer<SVONode> tree_nodes = ResourceDescriptorHeap[descriptor_indices.tree_nodes.x];
    StructuredBuffer<SVOLeaf> tree_leaves = ResourceDescriptorHeap[descriptor_indices.tree_leaves.x];
    RWStructuredBuffer<float> atlas = ResourceDescriptorHeap[descriptor_indices.atlas.x];
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;

    SVOWriteBuffer<float> svo_occupancy = SVOWriteBuffer<float>(SVO(tree_nodes, tree_leaves, svo_info, true), atlas);
    inject_svo_leaf_occupancy(svo_occupancy, p_thread_id, frame_slot);
    GroupMemoryBarrierWithGroupSync();
    // The thread with the top left corner is responsible for the write of the parent node.
    // We use p_group_thread_id < 32 instead of p_group_thread_id % 8 to try to concentrate as much thread in the same subgroup
    // possible.
    if (p_group_thread_id >= 32) {
        return;
    }
    // We extract the thread on the first corner, which is responsible for updating the state of its parent.
    // Because we have the workgroup grouped in 256 invocations, which is a multiple of 8, meaning we could safely assume that
    // all the 8 leaves shares the same parent node are allocated in the same workgroup.
    inject_svo_leaf_parent_occupancy(svo_occupancy, p_group_id * 256 + p_group_thread_id * 8, frame_slot);
}

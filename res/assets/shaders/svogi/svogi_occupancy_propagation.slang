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

#include "svogi/svogi_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "svo_pre_deferred_debug_info_buffer")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer", "read")]
    uint2 tree_nodes;
    [WBE_DESCRIPTOR("storage_buffer", "svo_leaf_buffer", "read")]
    uint2 tree_leaves;
    [WBE_DESCRIPTOR("storage_buffer", "atlas", "read_write")]
    uint2 atlas;
    [WBE_PUSH_CONSTANT("frame_slot")]
    uint shader_debug_frame_slot;
    /**
     * @brief Depth of the child nodes this dispatch reads; their parents, one level up, are written.
     */
    [WBE_PUSH_CONSTANT("child_depth")]
    uint child_depth;
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Propagates the occupancy one level up the tree, from the nodes at child_depth to their parents. The occupancy injection
// writes the leaves and their parents at depth max_depth - 1, so this pass is dispatched once for each child_depth from
// max_depth - 1 down to 2, in that order, with one invocation for each block of eight nodes.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint p_thread_id : SV_DispatchThreadID) {
    ConstantBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    StructuredBuffer<SVONode> tree_nodes = ResourceDescriptorHeap[descriptor_indices.tree_nodes.x];
    StructuredBuffer<SVOLeaf> tree_leaves = ResourceDescriptorHeap[descriptor_indices.tree_leaves.x];
    RWStructuredBuffer<float> occupancy = ResourceDescriptorHeap[descriptor_indices.atlas.x];
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;

    SVOWriteBuffer<float> svo_occupancy =
        SVOWriteBuffer<float>(SVO(tree_nodes, tree_leaves, svo_info, true), occupancy);
    // Nodes are allocated in blocks of eight siblings, so the invocation of a block is responsible for their parent.
    propagate_svo_occupancy(svo_occupancy, p_thread_id * 8, descriptor_indices.child_depth, frame_slot);
}

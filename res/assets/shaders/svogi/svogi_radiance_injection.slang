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
#include "prefix_sum/prefix_sum_utils.slang"
#include "utils/utils.slang"
#include "utils/counted_buffer.slang"
#include "light/spot_light_utils.slang"

struct DescriptorIndices {
    [WBE_DESCRIPTOR("uniform_buffer", "svo_pre_deferred_debug_info_buffer")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer", "read")]
    uint2 tree_nodes;
    [WBE_DESCRIPTOR("storage_buffer", "svo_leaf_buffer", "read")]
    uint2 tree_leaves;
    [WBE_DESCRIPTOR("storage_buffer", "index_buffer", "write")]
    uint2 index_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "staged_buffer", "write")]
    uint2 staged_buffer;
    [WBE_DESCRIPTOR("storage_buffer", "spot_light_infos", "read")]
    uint2 light_infos;
    [WBE_PUSH_CONSTANT("frame_slot")]
    uint shader_debug_frame_slot;
    LightParamDescriptorIndices lights[WBE_MAX_SPOT_LIGHT_COUNT];
};

[vk::push_constant]
ConstantBuffer<DescriptorIndices> descriptor_indices;

// Stage buffer is written in float3x3, where each row represents ir, ig, ib
groupshared float3x3 local_staged_buffer[256 * WBE_MAX_SPOT_LIGHT_COUNT];
groupshared uint2 local_index_buffer[256 * WBE_MAX_SPOT_LIGHT_COUNT];
groupshared uint buffer_tracker;

groupshared uint global_index_start;


[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 p_group_id : SV_GroupID, uint3 p_group_thread_id : SV_GroupThreadID) {
    uint2 dispatch_thread_id = z_order_swizzle_16x16(p_group_id.xy, p_group_thread_id.xy);
    ConstantBuffer<SVOInfo> svo_info = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    StructuredBuffer<SVONode> tree_nodes = ResourceDescriptorHeap[descriptor_indices.tree_nodes.x];
    StructuredBuffer<SVOLeaf> tree_leaves = ResourceDescriptorHeap[descriptor_indices.tree_leaves.x];
    CountedBuffer<uint2> index_buffer = CountedBuffer<uint2>(descriptor_indices.index_buffer, descriptor_indices.shader_debug_frame_slot);
    RWStructuredBuffer<SegmentedWrapper<float3x3>> staged_buffer = ResourceDescriptorHeap[descriptor_indices.staged_buffer.x];
    uint frame_slot = descriptor_indices.shader_debug_frame_slot;

    SVO svo = SVO(tree_nodes, tree_leaves, svo_info, true);
    uint spot_light_count = get_spot_light_count(descriptor_indices.light_infos);
    float2 uv = float2(dispatch_thread_id) / float2(uint2(16, 16) * WorkgroupCount().xy);
    if (all(p_group_thread_id.xy == uint2(0, 0))) {
        buffer_tracker = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    // We first gather all the voxels each invocation in this workgroup is going to write into, perform a sort on them, and
    // sum up the ones that are in the same voxel.
    for (uint spot_light_index = 0; spot_light_index < spot_light_count; ++spot_light_index) {
        SpotLight spot_light = SpotLight(descriptor_indices.light_infos, descriptor_indices.lights[spot_light_index], spot_light_index);
        // Read the position and the flux from the same shadow-map texel; the reflective shadow map shares its extent.
        uint2 shadow_size = spot_light.get_shadow_size();
        uint2 texel = min(uint2(uv * float2(shadow_size)), shadow_size - 1);
        float3 position = spot_light.shadow_texel_world_position(uv);
        SVONodeTraceInfo trace_info;
        uint data_index = svo.trace_data(position, trace_info);
        if (trace_info.is_node()) {
            // Nodes are ignored. We only trace leaves.
            continue;
        }
        float3 color = spot_light.rsm_flux.Load(int3(int2(texel), 0)).rgb;
        uint index;
        InterlockedAdd(buffer_tracker, 1, index);
        // We treat the flux as small VPLs. We inject those VPLs into the voxels.
        uint subvoxel_index;
        float3 subvoxel_position = svo_pos_to_subvoxel_pos(trace_info.svo_position, subvoxel_index);
        // We'll use the 3 most significant bit for recording the subvoxel index, and the rest are the data index.
        uint key = (subvoxel_index << (32 - 3)) | data_index;
        local_index_buffer[index] = uint2(key, index);
        float3x3 radiance_vector = float3x3(subvoxel_position * color.r,
                                            subvoxel_position * color.g,
                                            subvoxel_position * color.b);
        local_staged_buffer[index] = radiance_vector;
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(p_group_thread_id.xy == uint2(0, 0))) {
        global_index_start = index_buffer.atomic_expand(buffer_tracker);
    }
    GroupMemoryBarrierWithGroupSync();
    // We have a maximum of 256 * WBE_MAX_SPOT_LIGHT_COUNT indices to write. Each invocation is responsible of WBE_MAX_SPOT_LIGHT_COUNT.
    uint thread_id = p_group_thread_id.xy.flatten(16);
    [unroll] for (uint i = 0; i < WBE_MAX_SPOT_LIGHT_COUNT; ++i) {
        uint index = thread_id * 4 + i;
        if (index >= buffer_tracker) {
            return;
        }
        index_buffer.set(global_index_start + index, local_index_buffer[index]);
        staged_buffer[global_index_start + index] = {local_staged_buffer[index], 0};
    }
}

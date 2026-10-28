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
#include "svo/svo.slang"

struct SVODebugDrawDescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "svo_info", "read")]
    uint2 svo_info;
    [WBE_DESCRIPTOR("storage_buffer", "svo_node_buffer", "read")]
    uint2 tree_nodes;
    [WBE_DESCRIPTOR("storage_buffer", "svo_leaf_buffer", "read")]
    uint2 tree_leaves;

    [WBE_DESCRIPTOR("uniform_buffer", "gbuffer_camera_info")]
    uint2 camera_info;
    [WBE_PUSH_CONSTANT("frame_slot")]
    uint shader_debug_frame_slot;
};

/**
 * @brief Vertex output for SVO debug wireframes.
 */
struct SVODebugDrawVertexOutput {
    /**
     * @brief Clip-space vertex position.
     */
    float4 position : SV_Position;
    /**
     * @brief Wireframe color.
     */
    float4 color : COLOR0;
};

[vk::push_constant]
ConstantBuffer<SVODebugDrawDescriptorIndices> descriptor_indices;

/**
 * @brief Produces one of the eight corners of a leaf voxel or the SVO bounds without a vertex buffer.
 */
[shader("vertex")]
SVODebugDrawVertexOutput vertex_main(uint p_vertex_id : SV_VulkanVertexID, uint p_instance_id : SV_VulkanInstanceID) {
    StructuredBuffer<SVOInfo> svo_info_buffer = ResourceDescriptorHeap[descriptor_indices.svo_info.x];
    StructuredBuffer<SVONode> tree_nodes = ResourceDescriptorHeap[descriptor_indices.tree_nodes.x];
    StructuredBuffer<SVOLeaf> tree_leaves = ResourceDescriptorHeap[descriptor_indices.tree_leaves.x];
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    SVOInfo svo_info = svo_info_buffer[0];
    SVO svo = SVO(tree_nodes, tree_leaves, svo_info, true);

    uint3 corner = uint3((p_vertex_id >> 2) & 1, (p_vertex_id >> 1) & 1, p_vertex_id & 1);
    bool is_bounds = p_instance_id == svo.get_total_leaves();
    float3 world_position;
    if (is_bounds) {
        world_position = svo_info.origin + float3(corner) * svo_info.width;
    }
    else {
        if (!svo.is_leaf_occupied(p_instance_id, descriptor_indices.shader_debug_frame_slot)) {
            SVODebugDrawVertexOutput empty_output;
            empty_output.position = float4(2.0, 2.0, 2.0, 1.0);
            empty_output.color = float4(0.0);
            return empty_output;
        }
        uint3 leaf_grid_position = svo.get_leaf_grid_position(p_instance_id, descriptor_indices.shader_debug_frame_slot);
        float voxel_width = svo_info.width / float(1u << svo_info.max_depth);
        world_position = svo_info.origin + float3(leaf_grid_position + corner) * voxel_width;
    }
    float4 view_position =
        software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.view), float4(world_position, 1.0));
    SVODebugDrawVertexOutput output;
    output.position = software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.proj), view_position);
    output.color = is_bounds ? float4(1.0, 0.0, 0.0, 1.0) : float4(1.0);
    return output;
}

/**
 * @brief Colors each debug wireframe.
 */
[shader("fragment")]
float4 fragment_main(SVODebugDrawVertexOutput p_input) : SV_Target0 {
    return p_input.color;
}

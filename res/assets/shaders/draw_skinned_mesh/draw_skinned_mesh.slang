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

#include "skeleton/skeleton_utils.slang"
#include "utils.slang"

/**
 * @brief Vertex attributes read from the bind-pose vertex buffer.
 */
struct SkinnedMeshVertexInput {
    /**
     * @brief The bind-pose mesh-space position.
     */
    [WBE_VERTEX_INPUT("position")]
    float3 position;
    /**
     * @brief The indices of the skeleton joints the vertex binds to.
     */
    [WBE_VERTEX_INPUT("joint_binding_ids")]
    int4 joint_ids;
    /**
     * @brief The influence weight of each bound joint; the weights sum to 1.
     */
    [WBE_VERTEX_INPUT("joint_binding_weights")]
    float4 joint_weights;
};

/**
 * @brief Vertex shader output / fragment shader input of the skinned mesh pass.
 */
struct SkinnedMeshVSOutput {
    /**
     * @brief Clip-space position.
     */
    float4 position : SV_Position;
    /**
     * @brief The skinned model-space position.
     */
    float3 model_position : TEXCOORD0;
};

/**
 * @brief Descriptor indices of the resources used by the skinned mesh pass.
 */
struct DrawSkinnedMeshDescriptorIndices {
    /**
     * @brief The per-frame view and projection matrices.
     */
    [WBE_DESCRIPTOR("uniform_buffer", "skinned_mesh_camera_info")]
    uint2 camera_info;
    /**
     * @brief The skinning transform of every skeleton joint, mapping bind-pose mesh space to posed model space.
     */
    [WBE_DESCRIPTOR("storage_buffer", "skinned_mesh_skinning_transforms", "read")]
    uint2 skinning_transforms;
};

[vk::push_constant]
ConstantBuffer<DrawSkinnedMeshDescriptorIndices> descriptor_indices;

/**
 * @brief Skin a bind-pose vertex by its joint binding and transform it into clip space.
 * @param p_input The incoming vertex attributes.
 * @return The vertex output for the fragment stage.
 */
[shader("vertex")]
SkinnedMeshVSOutput vertex_main(SkinnedMeshVertexInput p_input) {
    ConstantBuffer<CameraInfo> camera_info = ResourceDescriptorHeap[descriptor_indices.camera_info.x];
    StructuredBuffer<SkinningTransform> skinning_transforms = ResourceDescriptorHeap[descriptor_indices.skinning_transforms.x];
    JointBinding joint_binding;
    joint_binding.joint_ids = p_input.joint_ids;
    joint_binding.joint_weights = p_input.joint_weights;
    SkinningTransform skinning_transform = evaluate_skinning_transform(skinning_transforms, joint_binding);
    float3 model_position = transform_skinned_point(skinning_transform, p_input.position);
    float4 view_position =
        software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.view), float4(model_position, 1.0));
    SkinnedMeshVSOutput output;
    output.position = software_mul_float4x4_vector(WBE_SOFTWARE_MATRIX_ROWS(camera_info.proj), view_position);
    output.model_position = model_position;
    return output;
}

/**
 * @brief Color the surface with a gradient over its skinned model-space position, so the bend stays visible.
 * @param p_input The interpolated vertex output.
 * @return The opaque surface color.
 */
[shader("fragment")]
float4 fragment_main(SkinnedMeshVSOutput p_input) : SV_Target0 {
    float height = saturate(p_input.model_position.y * 0.5);
    float3 color = lerp(float3(0.1, 0.3, 0.9), float3(1.0, 0.6, 0.1), height);
    color *= 0.6 + 0.4 * saturate(p_input.model_position.x + 0.5);
    return float4(color, 1.0);
}

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
/**
 * @file skeleton_utils.slang
 * @brief Utilities that transform vertices by the joints of a skeleton with linear blend skinning.
 */
#ifndef WBE_FILE_SKELETON_UTILS_SLANG
#define WBE_FILE_SKELETON_UTILS_SLANG

/**
 * @brief The number of joint influences stored per vertex in a joint binding.
 */
static const uint WBE_JOINT_BINDING_INFLUENCE_COUNT = 4;

/**
 * @brief The affine skinning transform of one joint: the posed global joint transform multiplied by the inverse of its
 * bind-pose global transform, so it moves bind-pose positions to posed positions.
 * @details Stored as the first three rows of the 4x4 matrix; the last row is (0, 0, 0, 1). Row storage avoids loading
 * whole matrices from descriptor-heap memory, see WBE_SOFTWARE_MATRIX_ROWS.
 */
struct SkinningTransform {
    /**
     * @brief The first row of the transform.
     */
    float4 row_0;
    /**
     * @brief The second row of the transform.
     */
    float4 row_1;
    /**
     * @brief The third row of the transform.
     */
    float4 row_2;
};

/**
 * @brief The joints influencing one vertex and the weights of their influences, as stored in a mesh joint binding.
 */
struct JointBinding {
    /**
     * @brief The indices of the influencing joints in the skeleton.
     */
    int4 joint_ids;
    /**
     * @brief The influence weight of each joint; the weights sum to 1.
     */
    float4 joint_weights;
};

/**
 * @brief Blend four skinning transforms by their weights.
 * @param p_transform_0 The transform of the first influence.
 * @param p_transform_1 The transform of the second influence.
 * @param p_transform_2 The transform of the third influence.
 * @param p_transform_3 The transform of the fourth influence.
 * @param p_weights The weight of each influence.
 * @return The weighted sum of the transforms.
 */
SkinningTransform blend_skinning_transforms(SkinningTransform p_transform_0,
    SkinningTransform p_transform_1,
    SkinningTransform p_transform_2,
    SkinningTransform p_transform_3,
    float4 p_weights) {
    SkinningTransform result;
    result.row_0 = p_transform_0.row_0 * p_weights.x + p_transform_1.row_0 * p_weights.y + p_transform_2.row_0 * p_weights.z +
                   p_transform_3.row_0 * p_weights.w;
    result.row_1 = p_transform_0.row_1 * p_weights.x + p_transform_1.row_1 * p_weights.y + p_transform_2.row_1 * p_weights.z +
                   p_transform_3.row_1 * p_weights.w;
    result.row_2 = p_transform_0.row_2 * p_weights.x + p_transform_1.row_2 * p_weights.y + p_transform_2.row_2 * p_weights.z +
                   p_transform_3.row_2 * p_weights.w;
    return result;
}

/**
 * @brief Evaluate the skinning transform of a vertex from the skinning transforms of the skeleton joints.
 * @param p_skinning_transforms The skinning transform of every skeleton joint, indexed by joint.
 * @param p_binding The joint binding of the vertex; every joint id must index p_skinning_transforms.
 * @return The blended transform that moves the bind-pose vertex to its posed position.
 */
SkinningTransform evaluate_skinning_transform(StructuredBuffer<SkinningTransform> p_skinning_transforms, JointBinding p_binding) {
    return blend_skinning_transforms(p_skinning_transforms[p_binding.joint_ids.x],
        p_skinning_transforms[p_binding.joint_ids.y],
        p_skinning_transforms[p_binding.joint_ids.z],
        p_skinning_transforms[p_binding.joint_ids.w],
        p_binding.joint_weights);
}

/**
 * @brief Transform a point by a skinning transform.
 * @param p_transform The skinning transform.
 * @param p_point The bind-pose point.
 * @return The posed point.
 */
float3 transform_skinned_point(SkinningTransform p_transform, float3 p_point) {
    float4 point = float4(p_point, 1.0);
    return float3(dot(p_transform.row_0, point), dot(p_transform.row_1, point), dot(p_transform.row_2, point));
}

/**
 * @brief Transform a direction, such as a normal or a tangent, by a skinning transform and normalize it.
 * @details Exact for normals only when the joints are rotated, translated, and uniformly scaled.
 * @param p_transform The skinning transform.
 * @param p_direction The bind-pose direction.
 * @return The normalized posed direction.
 */
float3 transform_skinned_direction(SkinningTransform p_transform, float3 p_direction) {
    return normalize(float3(
        dot(p_transform.row_0.xyz, p_direction), dot(p_transform.row_1.xyz, p_direction), dot(p_transform.row_2.xyz, p_direction)));
}

#endif

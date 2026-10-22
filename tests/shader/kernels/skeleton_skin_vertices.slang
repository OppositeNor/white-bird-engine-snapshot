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
 * @file skeleton_skin_vertices.slang
 * @brief Test kernel that skins one point and one direction per thread with the skeleton utilities.
 * @details Required define: VERTEX_COUNT.
 */
#include "skeleton/skeleton_utils.slang"

StructuredBuffer<SkinningTransform> skinning_transforms;
StructuredBuffer<JointBinding> joint_bindings;
StructuredBuffer<float4> points;
StructuredBuffer<float4> directions;
RWStructuredBuffer<float4> skinned_points;
RWStructuredBuffer<float4> skinned_directions;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 p_thread_id : SV_DispatchThreadID) {
    if (p_thread_id.x >= VERTEX_COUNT) {
        return;
    }
    SkinningTransform transform = evaluate_skinning_transform(skinning_transforms, joint_bindings[p_thread_id.x]);
    skinned_points[p_thread_id.x] = float4(transform_skinned_point(transform, points[p_thread_id.x].xyz), 1.0);
    skinned_directions[p_thread_id.x] = float4(transform_skinned_direction(transform, directions[p_thread_id.x].xyz), 0.0);
}

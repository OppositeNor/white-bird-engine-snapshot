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
 * @file present_pixel_sort.slang
 * @brief Fullscreen pass that presents the post-processed pixel-sorted image, letterboxed to keep its aspect ratio.
 * @details Draw it with 3 vertices and no vertex buffer.
 */
#include "utils.slang"

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

/** Descriptor indices of the resources used by the present pass. */
struct PresentPixelSortDescriptorIndices {
    /** The post-processed pixel-sorted image, from pixel_sorting_post_processing. */
    [WBE_DESCRIPTOR("sampled_image", "pixel_sorting_post_processing_output")] uint2 sorted_image;
    /** Sampler used to scale the sorted image to the render target. */
    [WBE_DESCRIPTOR("sampler", "pixel_sort_sampler")] uint2 sorted_sampler;
};

[[vk::push_constant]]
ConstantBuffer<PresentPixelSortDescriptorIndices> descriptor_indices;

[shader("vertex")]
VSOutput vertex_main(uint p_vertex_id : SV_VulkanVertexID) {
    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(0.0, 2.0),
        float2(2.0, 0.0)
    };
    VSOutput output;
    output.uv = uvs[p_vertex_id];
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragment_main(VSOutput p_input) : SV_Target0 {
    Texture2D<float4> sorted_image = ResourceDescriptorHeap[descriptor_indices.sorted_image.x];
    SamplerState sorted_sampler = SamplerDescriptorHeap[descriptor_indices.sorted_sampler.x];
    float2 image_extent;
    sorted_image.GetDimensions(image_extent.x, image_extent.y);
    // The uv spans [0, 1] across the render target, so its per-pixel derivative is the reciprocal of the target extent.
    float2 target_extent = 1.0 / abs(float2(ddx(p_input.uv.x), ddy(p_input.uv.y)));
    float2 scale = (image_extent / target_extent) / max(image_extent.x / target_extent.x, image_extent.y / target_extent.y);
    float2 uv = (p_input.uv - 0.5) / scale + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    return float4(sorted_image.SampleLevel(sorted_sampler, uv, 0.0).rgb, 1.0);
}

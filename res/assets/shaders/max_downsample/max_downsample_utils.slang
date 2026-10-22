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
#ifndef WBE_FILE_MAX_DOWNSAMPLE_UTILS_SLANG
#define WBE_FILE_MAX_DOWNSAMPLE_UTILS_SLANG

/**
 * @brief Compute one texel of the next mip level as the per-channel maximum of its source footprint.
 * @details The footprint is the 2x2 source region at twice the destination coordinate. The destination extent follows the
 * Vulkan mip rule max(source / 2, 1), so on an odd source axis the last destination texel also covers the remaining source
 * column or row, and a source axis of extent one is clamped; every source texel therefore contributes to the result.
 * @param p_source Source mip level.
 * @param p_source_extent Extent of the source mip level in texels.
 * @param p_destination_extent Extent of the destination mip level in texels.
 * @param p_destination_texel Destination texel coordinate; must lie inside the destination extent.
 * @return The per-channel maximum of the source texels covered by the destination texel.
 */
float4 max_downsample(Texture2D<float4> p_source, uint2 p_source_extent, uint2 p_destination_extent, uint2 p_destination_texel) {
    uint2 source_last = p_source_extent - 1;
    uint2 footprint_begin = min(p_destination_texel * 2, source_last);
    uint2 footprint_end = min(p_destination_texel * 2 + 1, source_last);
    if (p_destination_texel.x + 1 == p_destination_extent.x) {
        footprint_end.x = source_last.x;
    }
    if (p_destination_texel.y + 1 == p_destination_extent.y) {
        footprint_end.y = source_last.y;
    }
    float4 result = p_source.Load(int3(footprint_begin, 0));
    for (uint y = footprint_begin.y; y <= footprint_end.y; ++y) {
        for (uint x = footprint_begin.x; x <= footprint_end.x; ++x) {
            result = max(result, p_source.Load(int3(x, y, 0)));
        }
    }
    return result;
}

#endif

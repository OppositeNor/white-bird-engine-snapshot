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
#include "function/render_vk/rend_sampler_params_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <optional>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RendSamplerDescVK RendSamplerParamsVK::to_desc() const {
    RendSamplerDescVK result{
        .min_filter = to_vk_filter(min_filter),
        .mag_filter = to_vk_filter(mag_filter),
        .mipmap_mode = to_vk_sampler_mipmap_mode(mipmap_mode),
        .address_mode_u = to_vk_sampler_address_mode(address_mode_u),
        .address_mode_v = to_vk_sampler_address_mode(address_mode_v),
        .address_mode_w = to_vk_sampler_address_mode(address_mode_w),
        .mip_lod_bias = mip_lod_bias,
        .max_anisotropy = max_anisotropy,
        .compare_op = std::nullopt,
        .min_lod = min_lod,
        .max_lod = max_lod,
        .border_color = to_vk_border_color(border_color),
        .unnormalized_coordinates = unnormalized_coordinates,
    };
    if (compare_op.has_value()) {
        result.compare_op = to_vk_compare_op(*compare_op);
    }
    return result;
}

} // namespace WhiteBirdEngine

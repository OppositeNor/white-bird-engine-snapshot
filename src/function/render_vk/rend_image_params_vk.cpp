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
#include "function/render_vk/rend_image_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RendImageDescVK RendImageParamsVK::to_desc(const RendGraphContextVK& p_context) const {
    if (depth == 0) {
        throw std::runtime_error("Failed to convert render graph image parameters: depth must be non-zero.");
    }
    const VkExtent2D resolved_extent = extent.resolve(p_context);
    return RendImageDescVK{
        .usage = usage,
        .additional_usage = to_vk_image_usage_flags(additional_usage),
        .format = to_vk_format(format),
        .extent = VkExtent3D{.width = resolved_extent.width, .height = resolved_extent.height, .depth = depth},
        .image_type = to_vk_image_type(image_type),
        .mip_levels = mip_levels,
        .layers = layers,
        .per_frame = per_frame,
    };
}

} // namespace WhiteBirdEngine

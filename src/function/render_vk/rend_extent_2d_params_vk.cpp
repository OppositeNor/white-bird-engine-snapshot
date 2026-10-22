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
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

uint32_t scale_dimension(uint32_t p_dimension, float p_scale) {
    const double scaled = std::floor(static_cast<double>(p_dimension) * static_cast<double>(p_scale));
    return static_cast<uint32_t>(std::clamp(scaled, 1.0, static_cast<double>(UINT32_MAX)));
}

} // namespace

VkExtent2D RendExtent2DParamsVK::resolve(const RendGraphContextVK& p_context) const {
    if (width.has_value() != height.has_value()) {
        throw std::runtime_error("Failed to resolve render graph extent: width and height must be set together.");
    }
    if (width.has_value()) {
        if (*width == 0 || *height == 0) {
            throw std::runtime_error(
                std::format("Failed to resolve render graph extent: fixed extent {}x{} has a zero dimension.", *width, *height));
        }
        return VkExtent2D{.width = *width, .height = *height};
    }
    if (!std::isfinite(swap_chain_scale) || swap_chain_scale <= 0.0F) {
        throw std::runtime_error(std::format(
            "Failed to resolve render graph extent: swap_chain_scale must be positive and finite, got {}.", swap_chain_scale));
    }
    return VkExtent2D{
        .width = scale_dimension(p_context.swap_chain_extent.width, swap_chain_scale),
        .height = scale_dimension(p_context.swap_chain_extent.height, swap_chain_scale),
    };
}

} // namespace WhiteBirdEngine

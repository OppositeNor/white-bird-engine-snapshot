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
#include "function/render_vk/rt_params_clear_value_vk.hh"
#include <format>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

VkClearValue RTParamsClearValueVK::to_vk() const {
    if (color.has_value() == depth.has_value()) {
        throw std::runtime_error("Failed to convert render task clear value: set exactly one of color and depth.");
    }
    if (depth.has_value()) {
        return VkClearValue{.depthStencil = {.depth = *depth, .stencil = stencil}};
    }
    if (color->size() != 4) {
        throw std::runtime_error(
            std::format("Failed to convert render task clear value: color must have 4 components, got {}.", color->size()));
    }
    return VkClearValue{.color = {.float32 = {(*color)[0], (*color)[1], (*color)[2], (*color)[3]}}};
}

} // namespace WhiteBirdEngine

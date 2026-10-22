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
#include "function/render_vk/rend_image_view_params_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <optional>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

RendImageViewDescVK RendImageViewParamsVK::to_desc() const {
    RendImageViewDescVK result{
        .view_type = std::nullopt,
        .format = std::nullopt,
        .aspect_mask = std::nullopt,
        .base_mip_level = base_mip_level,
        .mip_level_count = mip_level_count,
        .base_array_layer = base_array_layer,
        .layer_count = layer_count,
    };
    if (view_type.has_value()) {
        result.view_type = to_vk_image_view_type(*view_type);
    }
    if (format.has_value()) {
        result.format = to_vk_format(*format);
    }
    if (aspect_mask.has_value()) {
        if (aspect_mask->empty()) {
            throw std::runtime_error("Failed to convert render graph image view parameters: aspect_mask must not be empty when set.");
        }
        result.aspect_mask = to_vk_image_aspect_flags(*aspect_mask);
    }
    return result;
}

} // namespace WhiteBirdEngine

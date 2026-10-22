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
#include "function/render_vk/rend_buffer_params_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <cstddef>
#include <stdexcept>

namespace WhiteBirdEngine {

RendBufferDescVK RendBufferParamsVK::to_desc() const {
    if (size == 0) {
        throw std::runtime_error("Failed to convert render graph buffer parameters: size must be non-zero.");
    }
    return RendBufferDescVK{
        .usage = usage,
        .additional_usage = to_vk_buffer_usage_flags(additional_usage),
        .size = static_cast<size_t>(size),
        .domain = domain,
        .per_frame = per_frame,
        .initial_data = {},
    };
}

} // namespace WhiteBirdEngine

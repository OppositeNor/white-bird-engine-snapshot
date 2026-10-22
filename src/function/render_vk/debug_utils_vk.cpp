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
#include "function/render_vk/debug_utils_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

void set_debug_object_name_vk(const RenderContextVK* p_render_data, VkObjectType p_type, uint64_t p_handle, std::string_view p_name) {
    if (p_render_data == nullptr || p_render_data->set_debug_utils_object_name == nullptr ||
        p_render_data->logic_device == VK_NULL_HANDLE || p_handle == 0 || p_name.empty()) {
        return;
    }
    // Vulkan reads a NUL-terminated name.
    const std::string name(p_name);
    const VkDebugUtilsObjectNameInfoEXT name_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext = nullptr,
        .objectType = p_type,
        .objectHandle = p_handle,
        .pObjectName = name.c_str(),
    };
    // Naming only aids debugging; a failure must not affect rendering.
    static_cast<void>(p_render_data->set_debug_utils_object_name(p_render_data->logic_device, &name_info));
}

} // namespace WhiteBirdEngine

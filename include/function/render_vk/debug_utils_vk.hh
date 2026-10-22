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
#ifndef WBE_FILE_DEBUG_UTILS_VK_HH
#define WBE_FILE_DEBUG_UTILS_VK_HH

#include "function/render_vk/render_context_vk.hh"
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @brief Name a Vulkan object through VK_EXT_debug_utils, for debuggers and validation messages.
 *
 * Does nothing when the extension is unavailable, the render data has no logical device yet, the handle is null, or the
 * name is empty. The object must not be used by
 * another thread during the call.
 *
 * @param p_render_data Render data holding the logical device and the naming command.
 * @param p_type The Vulkan type of the object.
 * @param p_handle The object handle, as Vulkan's 64-bit object handle value.
 * @param p_name The name.
 */
void set_debug_object_name_vk(const RenderContextVK* p_render_data, VkObjectType p_type, uint64_t p_handle, std::string_view p_name);

/**
 * @brief Name a Vulkan object; converts the handle to Vulkan's 64-bit object handle value.
 */
template <typename THandle>
void set_debug_object_name_vk(const RenderContextVK* p_render_data, VkObjectType p_type, THandle p_handle, std::string_view p_name) {
    if constexpr (std::is_null_pointer_v<THandle>) {
        set_debug_object_name_vk(p_render_data, p_type, uint64_t{0}, p_name);
    } else if constexpr (std::is_pointer_v<THandle>) {
        set_debug_object_name_vk(p_render_data, p_type, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_handle)), p_name);
    } else {
        set_debug_object_name_vk(p_render_data, p_type, static_cast<uint64_t>(p_handle), p_name);
    }
}

} // namespace WhiteBirdEngine

#endif

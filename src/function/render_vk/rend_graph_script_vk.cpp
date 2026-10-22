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
#include "function/render_vk/rend_graph_script_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "core/script/sb_lua.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include "generated/enum_reflection.gen.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_manager/smr_resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

DataValue RendGraphScriptVK::make_context(const RenderContextVK& p_render_data, const RendGraphContextVK& p_graph_context) {
    return serialize_context(get_recipe_context(p_render_data, p_graph_context));
}

RendRecipeContextVK RendGraphScriptVK::get_recipe_context(const RenderContextVK& p_render_data, const RendGraphContextVK& p_graph_context) {
    return RendRecipeContextVK{
        .frames_in_flight = p_render_data.frames_in_flight,
        .swap_chain_format = static_cast<ImageFormatVK>(static_cast<std::underlying_type_t<VkFormat>>(p_render_data.swap_chain_format)),
        .swap_chain_extent = {.width = p_graph_context.swap_chain_extent.width, .height = p_graph_context.swap_chain_extent.height},
    };
}

DataValue RendGraphScriptVK::run(std::string_view p_module_name, std::string_view p_source, const DataValue& p_context) {
    const SMRResourceManager module_resolver;
    SBLua backend;
    backend.set_module_resolver(&module_resolver);
    backend.load_module(p_module_name, p_source);
    const std::array<DataValue, 1> arguments{p_context};
    return backend.call(p_module_name, "build", arguments);
}

DataValue RendGraphScriptVK::run(std::string_view p_script_key, const DataValue& p_context) {
    ResourceManager* resource_manager = ResourceManager::get_singleton();
    if (resource_manager == nullptr) {
        throw std::runtime_error(std::format("Failed to run render graph script '{}': the resource manager is unavailable.", p_script_key));
    }
    RefRes<RUScript> script = resource_manager->get_resource<RUScript>(p_script_key);
    if (script == MEM_NULL || script->source == MEM_NULL) {
        throw std::runtime_error(std::format("Failed to run render graph script '{}': no script resource has this key.", p_script_key));
    }
    return run(p_script_key, std::string_view(script->source.get(), script->size - 1), p_context);
}

} // namespace WhiteBirdEngine

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
#include "function/render_vk/render_plugin_vk.hh"
#include "core/allocator/stl_allocator.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/descriptor_set_plugin_vk.hh"
#include "function/render_vk/ray_tracing_plugin_vk.hh"
#include "function/render_vk/timestamp_query_plugin_vk.hh"

#include "core/memory/reference_strong.hh"
#include "function/render_vk/render_context_vk.hh"

namespace WhiteBirdEngine {

void register_builtin_render_plugins_vk(Vector<Ref<RenderPluginVK>, RenderAllocator>& p_registered_plugins,
    RenderAllocator* p_allocator,
    const RenderContextVK* p_render_data,
    const RendererCapabilities& p_capabilities) {
    p_registered_plugins.clear();
    if (p_capabilities.ray_tracing || p_capabilities.ray_query) {
        p_registered_plugins.emplace_back(make_ref<RayTracingPluginVK>(p_allocator, p_render_data));
    }
    if (p_capabilities.timestamp_query) {
        p_registered_plugins.emplace_back(make_ref<TimestampQueryPluginVK>(p_allocator, p_render_data));
    }
    if (p_capabilities.descriptor_set) {
        p_registered_plugins.emplace_back(make_ref<DescriptorSetPluginVK>(p_allocator, p_render_data));
    }
}

} // namespace WhiteBirdEngine

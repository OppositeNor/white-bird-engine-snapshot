/* Copyright 2026 OppositeNor

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
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "core/memory/reference_strong.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/roc_single_vk.hh"
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

std::string IBindableRenderResourceVK::make_member_resource_id(std::string_view p_resource_id, std::string_view p_member) {
    return std::format("{}.{}", p_resource_id, p_member);
}

void IBindableRenderResourceVK::bind_render_object(
    RendGraphDescVK& p_graph, std::string_view p_resource_id, Ref<IRenderObjectVK> p_resource) {
    if (p_resource == MEM_NULL) {
        throw std::runtime_error(std::format("Failed to bind render resource '{}': render object is null.", p_resource_id));
    }
    if (p_resource->get_debug_name().empty()) {
        // Objects owned outside every registry would otherwise stay unnamed; a name given elsewhere is kept.
        p_resource->set_debug_name(p_resource_id);
    }
    // The container keeps resolving the imported object; recreating it rebinds the same object.
    bind_container(p_graph, p_resource_id, make_ref<ROCSingleVK>(p_graph.allocator, [resource = std::move(p_resource)](uint32_t /*p_slot*/) {
        return resource;
    }));
}

void IBindableRenderResourceVK::bind_container(
    RendGraphDescVK& p_graph, std::string_view p_resource_id, Ref<RenderObjectContainerVK> p_container) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("Failed to bind render resource: resource id is empty.");
    }
    if (p_container == MEM_NULL) {
        throw std::runtime_error(std::format("Failed to bind render resource '{}': container is null.", p_resource_id));
    }
    p_graph.add_resource(RendResVK{
        .res_id = std::string(p_resource_id),
        .res_type_id = p_container->get_resource_type_id(),
        .container = std::move(p_container),
    });
}

void IBindableRenderResourceVK::bind_resource_list(
    RendGraphDescVK& p_graph, std::string_view p_resource_id, Ref<RenderResourceListVK> p_resource_list) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("Failed to bind render resource list: resource id is empty.");
    }
    if (p_resource_list == MEM_NULL) {
        throw std::runtime_error(std::format("Failed to bind render resource list '{}': resource list is null.", p_resource_id));
    }
    p_graph.add_resource(RendResVK{
        .res_id = std::string(p_resource_id),
        .resource_list = std::move(p_resource_list),
    });
}

} // namespace WhiteBirdEngine

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
#include "function/render_vk/render_resource_list_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_defs.hh"
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

RenderResourceListVK::RenderResourceListVK(RenderAllocator* p_allocator) : resources(p_allocator) {
    if (p_allocator == nullptr) {
        throw std::runtime_error("Failed to construct render resource list: allocator is null.");
    }
}

RenderResourceListVK& RenderResourceListVK::add_resource(Ref<IRenderObject> p_resource) {
    if (p_resource == MEM_NULL) {
        throw std::runtime_error("Failed to add render resource list entry: resource is null.");
    }
    resources.push_back(std::move(p_resource));
    return *this;
}

size_t RenderResourceListVK::get_resource_count() const {
    return resources.size();
}

Ref<IRenderObject> RenderResourceListVK::acquire_resource(size_t p_index) const {
    if (p_index >= resources.size()) {
        throw std::runtime_error("Failed to acquire render resource list entry: index is out of bounds.");
    }
    return resources[p_index];
}

void RenderResourceListVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_resource_list(p_graph, p_resource_id, acquire_this_ref());
}

} // namespace WhiteBirdEngine

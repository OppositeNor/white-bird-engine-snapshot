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
#include "function/render/render_database.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/reference_weak.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_defs.hh"
#include <stdexcept>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {
RenderDatabase::RenderDatabase(AllocType* p_allocator)
    : compiled_render_resources(p_allocator) {
    singleton = this;
    if (p_allocator == nullptr) {
        throw std::runtime_error("Failed to create renderer database: allocator is null.");
    }
    allocator = p_allocator;
}

void RenderDatabase::register_compiled_render_resource(std::string_view p_resource_key, Ref<IRenderObject> p_render_object) {
    if (p_resource_key.empty()) {
        throw std::runtime_error("Failed to register compiled render resource: resource key is empty.");
    }
    if (p_render_object == MEM_NULL) {
        throw std::runtime_error("Failed to register compiled render resource: render object is null.");
    }
    compiled_render_resources.insert_or_assign(CompiledRenderResourceKey{.resource_key = std::string(p_resource_key),
                                                   .render_object_type_id = p_render_object->get_type_uuid()},
        RefWeak<IRenderObject>(p_render_object));
    p_render_object->set_debug_name(p_resource_key);
}

Ref<IRenderObject> RenderDatabase::get_compiled_render_resource(std::string_view p_resource_key, HashCode p_render_object_type_id) {
    auto entry = compiled_render_resources.find(
        CompiledRenderResourceKey{.resource_key = std::string(p_resource_key), .render_object_type_id = p_render_object_type_id});
    if (entry == compiled_render_resources.end() || entry->second.is_null()) {
        return MEM_NULL;
    }
    return entry->second.lock();
}

bool RenderDatabase::contains_compiled_render_resource(std::string_view p_resource_key, HashCode p_render_object_type_id) const {
    auto entry = compiled_render_resources.find(
        CompiledRenderResourceKey{.resource_key = std::string(p_resource_key), .render_object_type_id = p_render_object_type_id});
    return entry != compiled_render_resources.end() && entry->second.is_valid();
}

void RenderDatabase::clear_compiled_render_resources() {
    compiled_render_resources.clear();
}

} // namespace WhiteBirdEngine

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
#include "function/scene/world.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/reference_weak.hh"
#include "function/function_utils.hh"
#include "function/scene/scene_node.hh"
#include "utils/utils.hh"
#include <entt/entt.hpp>
#include <string_view>

namespace WhiteBirdEngine {

World::World(HeapAllocatorDefault* p_allocator)
    : allocator(p_allocator), component_lists(p_allocator), scene_nodes(p_allocator), render_graph_resource_key(p_allocator) {
}

World::~World() = default;

entt::sparse_set* World::get_component_list(TypeID p_component_type_id) {
    auto iter = component_lists.find(p_component_type_id);
    if (iter != component_lists.end()) {
        return iter->second;
    }
    auto* component_list = entities.storage(static_cast<entt::id_type>(p_component_type_id));
    if (component_list != nullptr) {
        component_lists.emplace(p_component_type_id, component_list);
    }
    return component_list;
}

Vector<TypeID, HeapAllocatorDefault> World::get_component_type_ids(EntityID p_entity) {
    cache_component_lists();
    Vector<TypeID, HeapAllocatorDefault> component_type_ids(allocator);
    for (const auto& [component_type_id, component_list] : component_lists) {
        if (component_list->contains(p_entity)) {
            component_type_ids.push_back(component_type_id);
        }
    }
    return component_type_ids;
}

void World::register_scene_node(Ref<SceneNode> p_scene_node) {
    scene_nodes.insert_or_assign(p_scene_node->get_entity(), RefWeak<SceneNode>(p_scene_node));
}

void World::unregister_scene_node(EntityID p_entity) {
    scene_nodes.erase(p_entity);
}

Ref<SceneNode> World::get_scene_node(EntityID p_entity) {
    auto iter = scene_nodes.find(p_entity);
    if (iter == scene_nodes.end()) {
        return Ref<SceneNode>();
    }
    return iter->second.lock();
}

void World::set_render_graph_resource_key(std::string_view p_resource_key) {
    render_graph_resource_key.assign(p_resource_key);
}

void World::cache_component_lists() {
    for (auto [component_type_id, component_list] : entities.storage()) {
        component_lists.insert_or_assign(component_type_id, &component_list);
    }
}

} // namespace WhiteBirdEngine
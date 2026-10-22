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
#include "function/scene/scene_node.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/function_utils.hh"
#include "function/scene/world.hh"
#include "utils/utils.hh"
#include <entt/entt.hpp>
#include <stdexcept>
#include <utility>

namespace WhiteBirdEngine {

SceneNode::~SceneNode() {
    destroy_entity();
}

SceneNode::SceneNode(SceneNode&& p_other) noexcept
    : name(std::move(p_other.name)), world(std::move(p_other.world)), entity(p_other.entity) {
    p_other.entity = entt::null;
}

SceneNode& SceneNode::operator=(SceneNode&& p_other) noexcept {
    destroy_entity();
    name = std::move(p_other.name);
    world = std::move(p_other.world);
    entity = p_other.entity;
    if (!this_ref.is_null()) {
        world->register_scene_node(this_ref.lock());
    }
    p_other.entity = entt::null;
    return *this;
}

void SceneNode::set_ref_of_this(Ref<SceneNode> p_ref_of_this) {
    ThisRef<SceneNode>::set_ref_of_this(p_ref_of_this);
    world->register_scene_node(std::move(p_ref_of_this));
}

SceneNode::SceneNode(Ref<World> p_world)
    : name(p_world->get_allocator()), world(std::move(p_world)), entity(world->get_entities().create()) {
}

void SceneNode::remove_component(TypeID p_component_type_id) {
    auto* component_list = world->get_component_list(p_component_type_id);
    if (component_list == nullptr || !component_list->contains(entity)) {
        throw std::runtime_error("Component to be removed not found.");
    }
    component_list->erase(entity);
}

bool SceneNode::has_component(TypeID p_component_type_id) {
    const auto* component_list = world->get_component_list(p_component_type_id);
    return component_list != nullptr && component_list->contains(entity);
}

Vector<TypeID, HeapAllocatorDefault> SceneNode::get_component_type_ids() {
    return world->get_component_type_ids(entity);
}

void SceneNode::destroy_entity() {
    if (world.is_null() || entity == entt::null) {
        return;
    }
    auto& entities = world->get_entities();
    if (entities.valid(entity)) {
        world->unregister_scene_node(entity);
        entities.destroy(entity);
    }
    entity = entt::null;
}

} // namespace WhiteBirdEngine

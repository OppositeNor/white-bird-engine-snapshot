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
#include "function/ecs/systems.hh"
#include "core/memory/reference_strong.hh"
#include "function/ecs/components.hh"
#include "function/function_utils.hh"
#include "function/scene/scene_node.hh"
#include "function/scene/world.hh"
#include "glm/matrix.hpp"
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>

namespace WhiteBirdEngine {

namespace {
void update_global_transform(entt::registry& p_registry, EntityID p_entity, ComponentTransformMats& p_transform_mats, int64_t p_version) {
    if (p_transform_mats.version == p_version) {
        // Transform up to date, return.
        return;
    }

    const ComponentHierarchy* hierarchy = p_registry.try_get<ComponentHierarchy>(p_entity);
    if ( // No hierarchy or no parent, treat as root.
        hierarchy == nullptr ||
        hierarchy->parent == entt::null
        // Parent invalid. Probably because parent does not have a transform
        // component, or it is removed. In both cases, only update global
        // transform to local transform.
        || !p_registry.all_of<ComponentTransformMats>(hierarchy->parent)) {
        p_transform_mats.global_transform = p_transform_mats.local_transform;
        p_transform_mats.version = p_version;
        return;
    }

    // Get parent info.
    ComponentTransformMats& parent_transform_mats = p_registry.get<ComponentTransformMats>(hierarchy->parent);
    if (parent_transform_mats.version != p_version) {
        update_global_transform(p_registry, hierarchy->parent, parent_transform_mats, p_version);
    }
    p_transform_mats.global_transform = parent_transform_mats.global_transform * p_transform_mats.local_transform;
    // Update the version to newest.
    p_transform_mats.version = p_version;
}

Ref<SceneNode> require_scene_node(World& p_world, EntityID p_entity) {
    Ref<SceneNode> scene_node = p_world.get_scene_node(p_entity);
    if (scene_node.is_null()) {
        throw std::runtime_error("ComponentDriver entity has no registered SceneNode.");
    }
    return scene_node;
}
} // namespace

void update_transform_mats(World& p_world, int64_t p_version) {
    // Update local transforms.
    auto local_transform_iterate_view = p_world.get_entities().view<const ComponentTransform, ComponentTransformMats>();
    for (auto [entity, transform, transform_mats] : local_transform_iterate_view.each()) {
        // Note that, for some reason, GLM decides to perform the transform
        // BEFORE the transform passed in (i.e. when declaring glm::translate(M,
        // T)), we are actually doing M(T(v)) but not T(M(v)). So the order:
        // scale, then rotate, then translate, we have to write it backwards
        // (translate(rotate(scale))).
        auto translation_mat = glm::translate(glm::mat4(1.0F), transform.translation);
        auto t_times_r_mat = translation_mat * glm::mat4_cast(transform.rotation);
        transform_mats.local_transform = glm::scale(t_times_r_mat, transform.scale);
    }

    // Update global transforms.
    auto global_transform_iterate_view = p_world.get_entities().view<ComponentTransformMats>();
    for (auto [entity, transform_mats] : global_transform_iterate_view.each()) {
        update_global_transform(p_world.get_entities(), entity, transform_mats, p_version);
    }
}

void update_camera_views(World& p_world) {
    auto camera_iterate_view = p_world.get_entities().view<ComponentCamera, const ComponentTransformMats>();
    for (auto [entity, camera, transform_mats] : camera_iterate_view.each()) {
        glm::mat4 camera_world_transform = transform_mats.global_transform * camera.local_transform;
        camera.view = glm::inverse(camera_world_transform);
    }
}

void system_driver_setup(World& p_world) {
    auto driver_view = p_world.get_entities().view<ComponentDriver>();
    for (auto [entity, driver] : driver_view.each()) {
        if (driver.setup != nullptr) {
            driver.setup(require_scene_node(p_world, entity));
        }
    }
}

void system_driver_ready(World& p_world) {
    auto driver_view = p_world.get_entities().view<ComponentDriver>();
    for (auto [entity, driver] : driver_view.each()) {
        if (driver.ready != nullptr) {
            driver.ready(require_scene_node(p_world, entity));
        }
    }
}

void system_driver_update(World& p_world, double p_delta) {
    auto driver_view = p_world.get_entities().view<ComponentDriver>();
    for (auto [entity, driver] : driver_view.each()) {
        if (driver.update != nullptr) {
            driver.update(require_scene_node(p_world, entity), p_delta);
        }
    }
}

void system_driver_teardown(World& p_world) {
    auto driver_view = p_world.get_entities().view<ComponentDriver>();
    for (auto [entity, driver] : driver_view.each()) {
        if (driver.teardown != nullptr) {
            driver.teardown(require_scene_node(p_world, entity));
        }
    }
}

} // namespace WhiteBirdEngine

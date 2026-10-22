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
#include "function/scene/scene.hh"
#include "core/allocator/i_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/ecs/systems.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_database.hh"
#include "function/render/render_resource_unit.hh"
#include "function/scene/world.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

Scene::Scene(HeapAllocatorDefault* p_allocator, Ref<IRenderer> p_renderer, std::string_view p_resource_manifest_key)
    : allocator(p_allocator), renderer(std::move(p_renderer)), worlds(p_allocator), resource_manifest_key(p_allocator) {
    if (allocator == nullptr) {
        throw std::runtime_error("Scene allocator cannot be null.");
    }
    if (renderer == MEM_NULL) {
        throw std::runtime_error("Scene renderer cannot be null.");
    }
    resource_manifest_key.assign(p_resource_manifest_key);
}

Scene::~Scene() = default;

void Scene::add_world(Ref<World> p_world) {
    if (lifecycle_state != LifecycleState::CONSTRUCTED) {
        throw std::runtime_error("Worlds can only be added before scene setup.");
    }
    if (p_world == MEM_NULL) {
        throw std::runtime_error("Scene world cannot be null.");
    }
    worlds.push_back(std::move(p_world));
}

Ref<World> Scene::get_world(size_t p_world_index) const {
    if (p_world_index >= worlds.size()) {
        throw std::runtime_error("Scene world index is out of range.");
    }
    return worlds[p_world_index];
}

void Scene::set_active_world(size_t p_world_index) {
    if (p_world_index >= worlds.size()) {
        throw std::runtime_error("Scene active world index is out of range.");
    }
    active_world_index = p_world_index;
}

Ref<World> Scene::get_active_world() const {
    if (worlds.empty()) {
        return Ref<World>();
    }
    return worlds[active_world_index];
}

void Scene::setup() {
    if (lifecycle_state != LifecycleState::CONSTRUCTED && lifecycle_state != LifecycleState::TORN_DOWN) {
        throw std::runtime_error("Scene setup requires a constructed or torn-down scene.");
    }
    for (size_t i = 0; i < worlds.size(); ++i) {
        std::string_view graph_key = worlds[i]->get_render_graph_resource_key();
        if (!graph_key.empty() && is_first_graph_key(i)) {
            renderer->register_render_graph(graph_key);
        }
        system_driver_setup(*worlds[i]);
    }
    lifecycle_state = LifecycleState::SETUP;
}

void Scene::ready() {
    if (lifecycle_state != LifecycleState::SETUP) {
        throw std::runtime_error("Scene ready requires setup to complete first.");
    }
    for (auto& world : worlds) {
        system_driver_ready(*world);
    }
    lifecycle_state = LifecycleState::READY;
}

void Scene::update(double p_delta) {
    if (lifecycle_state != LifecycleState::READY) {
        throw std::runtime_error("Scene update requires a ready scene.");
    }

    Ref<World> active_world = get_active_world();
    if (active_world != MEM_NULL) {
        std::string_view graph_key = active_world->get_render_graph_resource_key();
        if (!graph_key.empty()) {
            renderer->activate_render_graph(graph_key);
        }
    }

    for (auto& world : worlds) {
        system_driver_update(*world, p_delta);
    }
}

void Scene::teardown() {
    if (lifecycle_state != LifecycleState::SETUP && lifecycle_state != LifecycleState::READY) {
        throw std::runtime_error("Scene teardown requires a setup or ready scene.");
    }
    for (size_t i = worlds.size(); i > 0; --i) {
        system_driver_teardown(*worlds[i - 1]);
    }
    for (size_t i = worlds.size(); i > 0; --i) {
        std::string_view graph_key = worlds[i - 1]->get_render_graph_resource_key();
        if (!graph_key.empty() && is_first_graph_key(i - 1)) {
            renderer->unregister_render_graph(graph_key);
        }
    }
    lifecycle_state = LifecycleState::TORN_DOWN;
}

bool Scene::is_first_graph_key(size_t p_world_index) const {
    std::string_view graph_key = worlds[p_world_index]->get_render_graph_resource_key();
    for (size_t i = 0; i < p_world_index; ++i) {
        if (worlds[i]->get_render_graph_resource_key() == graph_key) {
            return false;
        }
    }
    return true;
}

} // namespace WhiteBirdEngine
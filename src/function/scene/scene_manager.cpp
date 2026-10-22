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
#include "function/scene/scene_manager.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "function/scene/scene.hh"
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

SceneManager::SceneManager(Map<std::string, Ref<Scene>, HeapAllocatorGlobal> p_scene_map)
    : scene_map(std::move(p_scene_map)), current_scene_id(global_allocator()) {
}

SceneManager::~SceneManager() {
    if (current_scene != MEM_NULL) {
        current_scene->teardown();
    }
}

Ref<Scene> SceneManager::get_current_scene() {
    return current_scene;
}

Ref<const Scene> SceneManager::get_current_scene() const {
    return current_scene;
}

void SceneManager::change_scene(std::string_view p_scene_id) {
    if (current_scene != MEM_NULL && current_scene_id == p_scene_id) {
        return;
    }

    auto scene = scene_map.find(p_scene_id);
    if (scene == scene_map.end()) {
        throw std::runtime_error(std::string("Scene was not found: ") + std::string(p_scene_id));
    }

    if (current_scene != MEM_NULL) {
        current_scene->teardown();
    }
    current_scene = scene->second;
    current_scene_id.assign(p_scene_id);
    current_scene->setup();
    current_scene->ready();
}

} // namespace WhiteBirdEngine
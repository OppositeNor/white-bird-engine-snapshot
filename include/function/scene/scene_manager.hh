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
#ifndef WBE_FILE_SCENE_MANAGER_HH
#define WBE_FILE_SCENE_MANAGER_HH

#include "core/allocator/stl_allocator.hh"
#include "core/engine_core.hh"
#include "core/memory/reference_strong.hh"
#include "i_scene_manager.hh"
#include "scene.hh"
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class SceneManager
 * @brief scene manager.
 *
 */
class SceneManager final : public ISceneManager {
public:
    /** @brief Tear down the current scene before releasing it. */
    virtual ~SceneManager() override;
    SceneManager(const SceneManager&) = delete;
    SceneManager(SceneManager&&) noexcept = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager& operator=(SceneManager&&) noexcept = delete;

    /**
     * @brief Construct a manager over the supplied scenes.
     *
     * @param p_scene_map Strong scene references indexed by scene id; ownership is transferred to the manager.
     */
    SceneManager(Map<std::string, Ref<Scene>, HeapAllocatorGlobal> p_scene_map);

    virtual Ref<Scene> get_current_scene() override;

    virtual Ref<const Scene> get_current_scene() const override;

    /**
     * @brief Tear down the current scene, then set up and ready the requested scene.
     *
     * @param p_scene_id The id of the scene to activate.
     */
    virtual void change_scene(std::string_view p_scene_id) override;

private:
    Map<std::string, Ref<Scene>, HeapAllocatorGlobal> scene_map;
    String<HeapAllocatorGlobal> current_scene_id;
    Ref<Scene> current_scene;
};

} // namespace WhiteBirdEngine

#endif

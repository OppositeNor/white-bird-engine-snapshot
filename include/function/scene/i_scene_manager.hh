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
#ifndef WBE_FILE_I_SCENE_MANAGER_HH
#define WBE_FILE_I_SCENE_MANAGER_HH

#include "core/memory/reference_strong.hh"
#include "function/scene/scene.hh"
#include "utils/interface/i_singleton.hh"
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class ISceneManager
 * @brief Manages scene create, change and destroy.
 *
 */
class ISceneManager : public ISingleton<ISceneManager> {
public:
    ISceneManager() = default;
    virtual ~ISceneManager() = default;
    ISceneManager(const ISceneManager&) = delete;
    ISceneManager(ISceneManager&&) noexcept = delete;
    ISceneManager& operator=(const ISceneManager&) = delete;
    ISceneManager& operator=(ISceneManager&&) noexcept = delete;

    /**
     * @brief Get the current scene.
     *
     * @return The current scene.
     */
    virtual Ref<Scene> get_current_scene() = 0;

    /**
     * @brief Get the current scene.
     *
     * @return The current scene.
     */
    virtual Ref<const Scene> get_current_scene() const = 0;

    /**
     * @brief Change the scene according to scene ID.
     *
     * @param p_scene_id
     */
    virtual void change_scene(std::string_view p_scene_id) = 0;
};

} // namespace WhiteBirdEngine

#endif

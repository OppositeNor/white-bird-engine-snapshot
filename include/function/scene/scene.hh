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
#ifndef WBE_FILE_SCENE_HH
#define WBE_FILE_SCENE_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_resource_unit.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace WhiteBirdEngine {

class IRenderer;
class World;

/**
 * @class Scene
 * @brief Orchestrates the lifecycle and active render state of a set of worlds.
 *
 * The allocator must outlive the scene. The scene retains strong references to
 * its renderer and worlds and is neither copyable nor movable.
 */
class Scene {
public:
    /**
     * @brief Construct an empty scene.
     *
     * @param p_allocator Allocator used by scene-owned containers and strings.
     * @param p_renderer Renderer receiving graph lifecycle and world render state.
     * @param p_resource_manifest_key Resource manifest associated with this scene.
     */
    Scene(HeapAllocatorDefault* p_allocator, Ref<IRenderer> p_renderer, std::string_view p_resource_manifest_key = {});
    ~Scene();
    Scene(const Scene&) = delete;
    Scene(Scene&&) noexcept = delete;
    Scene& operator=(const Scene&) = delete;
    Scene& operator=(Scene&&) noexcept = delete;

    /**
     * @brief Add a world before scene setup.
     *
     * @param p_world The world to add to the scene.
     */
    void add_world(Ref<World> p_world);

    /**
     * @brief Get the number of worlds owned by this scene.
     *
     * @return The number of worlds in the scene.
     */
    size_t get_world_count() const {
        return worlds.size();
    }

    /**
     * @brief Get a world by index.
     *
     * @param p_world_index The zero-based world index.
     * @return The world at the requested index.
     */
    Ref<World> get_world(size_t p_world_index) const;

    /**
     * @brief Select the world whose graph and static mesh are published by update.
     *
     * @param p_world_index The zero-based index of the world to select.
     */
    void set_active_world(size_t p_world_index);

    /**
     * @brief Get the selected world, or null when this scene has no worlds.
     *
     * @return The selected world, or null when the scene has no worlds.
     */
    Ref<World> get_active_world() const;

    /**
     * @brief Get the scene resource manifest key.
     *
     * @return The resource-root-relative manifest key associated with this scene.
     */
    std::string_view get_resource_manifest_key() const {
        return resource_manifest_key;
    }

    /**
     * @brief Register world graphs and run every driver setup callback.
     */
    void setup();

    /**
     * @brief Run every driver ready callback after setup completes.
     */
    void ready();

    /**
     * @brief Publish active-world render state and update all world drivers.
     *
     * @param p_delta The elapsed time since the previous update, in seconds.
     */
    void update(double p_delta);

    /**
     * @brief Run driver teardown and release registered world graphs.
     */
    void teardown();

private:
    enum class LifecycleState : uint8_t {
        CONSTRUCTED,
        SETUP,
        READY,
        TORN_DOWN,
    };

    bool is_first_graph_key(size_t p_world_index) const;

    HeapAllocatorDefault* allocator;
    Ref<IRenderer> renderer;
    Vector<Ref<World>, HeapAllocatorDefault> worlds;
    String<HeapAllocatorDefault> resource_manifest_key;
    size_t active_world_index = 0;
    LifecycleState lifecycle_state = LifecycleState::CONSTRUCTED;
};

} // namespace WhiteBirdEngine

#endif

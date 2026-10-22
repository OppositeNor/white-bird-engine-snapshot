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
#ifndef WBE_FILE_SCENE_RESOURCE_UNIT_HH
#define WBE_FILE_SCENE_RESOURCE_UNIT_HH

#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <string>

namespace WhiteBirdEngine {

/**
 * @class RUSchemaComponent
 * @brief Placeholder scheme for a serialized component resource.
 *
 * @todo Define the component type and value representation.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaComponent : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaComponent)
};

/**
 * @class RUSchemaPrefab
 * @brief Placeholder scheme for a prefab resource.
 *
 * @todo Define node hierarchy and component references.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaPrefab : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaPrefab)
};

/**
 * @class RUSchemaWorldStaticComponent
 * @brief Static, non-simulated content of a world.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaWorldStaticComponent {
    /**
     * @brief Non-empty resource id of the static mesh rendered by the world.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string static_mesh_render;
};

/**
 * @class RUSchemaWorld
 * @brief The scheme of RUWorld.
 *
 * @todo Define render-graph, mesh, prefab, and component references.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaWorld : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaWorld)
    /**
     * @brief Static content of the world.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaWorldStaticComponent static_component;
};

/**
 * @class RUSchemaScene
 * @brief Placeholder scheme for a scene resource.
 *
 * @todo Define world references and initial active-world selection.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaScene : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaScene)
};

/**
 * @class RUComponent
 * @brief Runtime marker for a component resource.
 *
 * @todo Define the runtime component representation.
 */
struct RUComponent : public ResourceUnit {};

/**
 * @class RUPrefab
 * @brief Runtime marker for a prefab resource.
 *
 * @todo Define the runtime prefab representation.
 */
struct RUPrefab : public ResourceUnit {};

/**
 * @class RUWorldStaticComponent
 * @brief Static, non-simulated content of a world resource.
 *
 * Non-owning view into its resource chunk; copyable and movable with shallow chunk references.
 */
struct RUWorldStaticComponent {
    /** Chunk-owned, non-empty, NUL-terminated resource id of the static mesh rendered by the world. */
    RefRes<char> static_mesh_render;
};

/**
 * @class RUWorld
 * @brief World resource unit containing the data of a world.
 *
 * Non-owning view into its resource chunk; copyable and movable with shallow chunk references.
 *
 * @todo Define render-graph, mesh, prefab, and component references.
 */
struct RUWorld : public ResourceUnit {
    /** Static content of the world. */
    RUWorldStaticComponent static_component;
};

/**
 * @class RUScene
 * @brief Runtime marker for a scene resource.
 *
 * @todo Define the runtime scene representation.
 */
struct RUScene : public ResourceUnit {};

} // namespace WhiteBirdEngine

#endif
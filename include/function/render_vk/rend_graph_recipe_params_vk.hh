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
#ifndef WBE_FILE_REND_GRAPH_RECIPE_PARAMS_VK_HH
#define WBE_FILE_REND_GRAPH_RECIPE_PARAMS_VK_HH

#include "core/data/data_value.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_buffer_params_vk.hh"
#include "function/render_vk/rend_image_params_vk.hh"
#include "function/render_vk/rend_image_view_params_vk.hh"
#include "function/render_vk/rend_sampler_params_vk.hh"
#include <optional>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RendRecipeImageViewVK
 * @brief An image view declared by a render graph recipe. Copyable and movable; strict.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendRecipeImageViewVK {
    /**
     * @brief Graph resource id of the viewed image: a recipe image or an import.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string image;
    /**
     * @brief The view configuration; unset fields are derived from the image.
     */
    WBE_META(WBE_REFLECT)
    RendImageViewParamsVK view;
};

/**
 * @class RendRecipeResourceVK
 * @brief A graph-owned resource declared by a render graph recipe; exactly one kind is set. Copyable and movable; strict.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendRecipeResourceVK {
    /**
     * @brief Graph resource id.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string id;
    /**
     * @brief Set to declare a buffer.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendBufferParamsVK> buffer;
    /**
     * @brief Set to declare an image.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendImageParamsVK> image;
    /**
     * @brief Set to declare an image view.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendRecipeImageViewVK> image_view;
    /**
     * @brief Set to declare a sampler.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendSamplerParamsVK> sampler;
};

/**
 * @class RendRecipeEdgeVK
 * @brief A resource dependency edge from a recipe task to a later task; see RendTaskDescVK::connect(). Copyable and
 * movable; strict.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendRecipeEdgeVK {
    /**
     * @brief Id of the later task.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string to;
    /**
     * @brief Graph resource id the edge orders.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string resource;
};

/**
 * @class RendRecipeTriggerVK
 * @brief Makes a recipe task event-triggered; see RTTriggeredVK::wrap_factory(). Copyable and movable; strict.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendRecipeTriggerVK {
    /**
     * @brief Event id the task receives RTTriggeredVK::Command events under.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string event;
    /**
     * @brief Whether the task records on its first frame.
     */
    WBE_META(WBE_REFLECT)
    bool activate = false;
};

/**
 * @class RendRecipeToggleVK
 * @brief Makes a recipe task event-toggled; see RTToggleVK::wrap_factory(). Copyable and movable; strict.
 *
 * The task records on every frame while enabled. A rebuild re-runs the recipe, so pass the application's current state
 * through the context to keep it across rebuilds.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendRecipeToggleVK {
    /**
     * @brief Event id the task receives RTToggleVK::Command events under.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string event;
    /**
     * @brief Whether the task records until a command changes the state.
     */
    WBE_META(WBE_REFLECT)
    bool enabled = false;
};

/**
 * @class RendRecipeTaskVK
 * @brief A task of a render graph recipe. Copyable and movable; strict.
 *
 * A task either has a registered `type` and its `params`, or is an import (`import = true`): a task the caller supplies
 * through RendGraphRecipeImportsVK::add_task() under the same id, which the recipe can only `connect`, or is a subgraph
 * (`subgraph`): a nested recipe added with RendGraphDescVK::add_subgraph(), which other tasks connect to by this id.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendRecipeTaskVK {
    /**
     * @brief Task id, unique in the recipe.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string id;
    /**
     * @brief A task type name registered in RenderTaskRegistryVK; required unless the task is an import.
     */
    WBE_META(WBE_REFLECT)
    std::string type;
    /**
     * @brief Whether the caller supplies the task; an import sets no type, params, aliases, trigger, or toggle.
     */
    WBE_META(WBE_REFLECT)
    bool import = false;
    /**
     * @brief The task parameters, deserialized by the registry into the task type's parameter struct.
     */
    WBE_META(WBE_REFLECT)
    DataValue params;
    /**
     * @brief Map from task-local resource id to graph resource id; see RendTaskDescVK::alias().
     */
    WBE_META(WBE_REFLECT)
    DataValue aliases;
    /**
     * @brief Resource dependency edges to later tasks.
     */
    WBE_META(WBE_REFLECT)
    std::vector<RendRecipeEdgeVK> connect;
    /**
     * @brief Set to make the task event-triggered.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendRecipeTriggerVK> trigger;
    /**
     * @brief Set to make the task event-toggled. With a trigger too, the toggle gates the triggered task: it records on
     * the triggered frames that fall while the toggle is enabled.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendRecipeToggleVK> toggle;
    /**
     * @brief A nested RendGraphRecipeVK that makes this task a subgraph, which sets no type, params, aliases, or import. Its
     * trigger and toggle apply to every task of the subgraph.
     */
    WBE_META(WBE_REFLECT)
    DataValue subgraph;
    /**
     * @brief Map from each port of the subgraph to the resource it is bound to; see RendGraphDescVK::add_subgraph().
     */
    WBE_META(WBE_REFLECT)
    DataValue ports;
};

/**
 * @class RendGraphRecipeVK
 * @brief A render graph recipe: what a recipe script returns. Copyable and movable; strict.
 *
 * See RendGraphRecipeBuilderVK for how a recipe becomes a RendGraphDescVK.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RendGraphRecipeVK {
    /**
     * @brief Names of the resources the caller must supply; every supplied resource must be listed.
     */
    WBE_META(WBE_REFLECT)
    std::vector<std::string> imports;
    /**
     * @brief Graph-owned resources.
     */
    WBE_META(WBE_REFLECT)
    std::vector<RendRecipeResourceVK> resources;
    /**
     * @brief Tasks.
     */
    WBE_META(WBE_REFLECT)
    std::vector<RendRecipeTaskVK> tasks;
    /**
     * @brief The ports of a recipe used as a subgraph: resource ids its tasks use for resources of the enclosing graph.
     * Only a nested recipe sets them.
     */
    WBE_META(WBE_REFLECT)
    std::vector<std::string> ports;
};

} // namespace WhiteBirdEngine

#endif

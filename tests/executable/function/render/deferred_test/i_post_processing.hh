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
#ifndef WBE_FILE_I_POST_PROCESSING_HH
#define WBE_FILE_I_POST_PROCESSING_HH

#include "core/data/data_value.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "function/render_vk/render_vk_defs.hh"
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class IPostProcessing
 * @brief Interface of a post-processing effect of the deferred rendering test that modifies a color image in place.
 * @details Effects are disabled on construction. A disabled effect keeps its tasks in every graph it was added to, but
 * they record no commands, so the color image passes through unchanged. The effect's tasks are added by a Lua recipe
 * module (see configure_recipe()) and toggled by the effect's toggle event: set_enabled() emits the new state under it,
 * so the tasks of every graph switch together, and a rebuild re-runs the recipe with the current state. Change the
 * state only between frames. Neither copyable nor movable. All methods and destruction are confined
 * to the rendering thread, and the effect must outlive every graph it was added to.
 */
class IPostProcessing {
public:
    IPostProcessing() = delete;
    /**
     * @brief Constructor.
     * @param p_allocator Allocator of the effect's tasks; must outlive the effect.
     * @param p_toggle_event_id Event id the effect's tasks receive RTToggleVK::Command events under; non-empty.
     * @throws std::runtime_error If p_allocator is null or p_toggle_event_id is empty.
     */
    IPostProcessing(RenderAllocator* p_allocator, std::string_view p_toggle_event_id);
    virtual ~IPostProcessing() = default;
    IPostProcessing(const IPostProcessing&) = delete;
    IPostProcessing(IPostProcessing&&) = delete;
    IPostProcessing& operator=(const IPostProcessing&) = delete;
    IPostProcessing& operator=(IPostProcessing&&) = delete;

    /**
     * @brief Human-readable name of the effect.
     */
    virtual std::string_view get_name() const = 0;

    /**
     * @brief Describe the effect to a recipe that includes it, and supply the resources its tasks import.
     *
     * Writes `module`, the name of the Lua module whose `add(graph, params, target)` adds the effect's resources and tasks
     * to a recipe under construction, and every parameter that module reads, including `toggle_event` and `enabled`. The
     * recipe passes the map to `add()` as `params`.
     *
     * @param p_parameters The map to write the module name and parameters into.
     * @param p_imports The imports of the recipe, to supply the effect's application-owned resources to.
     */
    virtual void configure_recipe(DataValue& p_parameters, RendGraphRecipeImportsVK& p_imports) const = 0;

    /**
     * @brief Whether the effect's tasks record their commands.
     */
    bool is_enabled() const {
        return enabled;
    }

    /**
     * @brief Enable or disable the effect from the next recorded frame, by emitting the state under the toggle event.
     * @param p_enabled Whether the effect's tasks record their commands.
     */
    void set_enabled(bool p_enabled);

    /**
     * @brief Get the event id the effect's tasks receive RTToggleVK::Command events under.
     */
    std::string_view get_toggle_event_id() const {
        return toggle_event_id;
    }

protected:
    /**
     * @brief Get the allocator of the effect's tasks.
     */
    RenderAllocator* get_allocator() const {
        return allocator;
    }

private:
    RenderAllocator* allocator = nullptr;
    std::string toggle_event_id;
    bool enabled = false;
};

} // namespace WhiteBirdEngine

#endif

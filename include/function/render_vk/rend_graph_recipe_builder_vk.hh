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
#ifndef WBE_FILE_REND_GRAPH_RECIPE_BUILDER_VK_HH
#define WBE_FILE_REND_GRAPH_RECIPE_BUILDER_VK_HH

#include "core/data/data_value.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RendGraphRecipeImportsVK
 * @brief The resources and tasks a caller supplies to a render graph recipe, by import name and task id. Copyable and
 * movable.
 *
 * Holds references to resources: every supplied resource must outlive RendGraphRecipeBuilderVK::build(), which binds it
 * into the graph descriptor (the descriptor then retains it). Tasks are held by value and copied into the descriptor.
 */
class RendGraphRecipeImportsVK final {
public:
    RendGraphRecipeImportsVK() = default;
    ~RendGraphRecipeImportsVK() = default;
    RendGraphRecipeImportsVK(const RendGraphRecipeImportsVK&) = default;
    RendGraphRecipeImportsVK(RendGraphRecipeImportsVK&&) noexcept = default;
    RendGraphRecipeImportsVK& operator=(const RendGraphRecipeImportsVK&) = default;
    RendGraphRecipeImportsVK& operator=(RendGraphRecipeImportsVK&&) noexcept = default;

    /**
     * @brief Supply a resource under an import name; it is bound into the graph under that name.
     * @return This table.
     * @throws std::runtime_error If p_name is empty or already supplied.
     */
    RendGraphRecipeImportsVK& add(std::string_view p_name, const IBindableRenderResourceVK& p_resource);

    /**
     * @brief Get a supplied resource, or null when no resource is supplied under the name.
     */
    const IBindableRenderResourceVK* find(std::string_view p_name) const;

    /**
     * @brief Get every supplied import name, in sorted order.
     */
    std::vector<std::string> get_names() const;

    /**
     * @brief Supply a task under its task id, for a recipe task entry with `import = true` and the same id.
     *
     * The task keeps the factory, aliases, and edges the caller gave it; the recipe can add edges from it with `connect`.
     *
     * @param p_task The task.
     * @return This table.
     * @throws std::runtime_error If the task id is empty or already supplied.
     */
    RendGraphRecipeImportsVK& add_task(RendTaskDescVK p_task);

    /**
     * @brief Get a supplied task, or null when no task is supplied under the id.
     */
    const RendTaskDescVK* find_task(std::string_view p_task_id) const;

    /**
     * @brief Get every supplied task id, in sorted order.
     */
    std::vector<std::string> get_task_ids() const;

private:
    std::map<std::string, const IBindableRenderResourceVK*, std::less<>> resources;
    std::map<std::string, RendTaskDescVK, std::less<>> tasks;
};

/**
 * @class RendGraphRecipeBuilderVK
 * @brief Builds a RendGraphDescVK from a render graph recipe (RendGraphRecipeVK as a DataValue). Stateless; copyable.
 *
 * The recipe's imports are bound from the caller's table, its resources are declared as graph-owned buffers, images,
 * image views, and samplers, and its tasks are added through RenderTaskRegistryVK::add_task() (so task types'
 * prepare_graph() hooks run), then aliased, connected, and optionally wrapped as triggered or toggled tasks. Imported tasks are
 * copied from the caller's table and connected like the others. A task with a nested recipe in `subgraph` is built the same
 * way and added with RendGraphDescVK::add_subgraph(); a nested recipe may declare imports and import supplied tasks too, and
 * every supplied import and task must be declared at some level. Graph-level validation
 * (the DAG and resource-conflict rules) happens when the descriptor is built into a RenderGraphVK. Call on the rendering
 * thread, since task types may compile resources while preparing the graph.
 */
class RendGraphRecipeBuilderVK final {
public:
    RendGraphRecipeBuilderVK() = default;
    ~RendGraphRecipeBuilderVK() = default;
    RendGraphRecipeBuilderVK(const RendGraphRecipeBuilderVK&) = default;
    RendGraphRecipeBuilderVK(RendGraphRecipeBuilderVK&&) noexcept = default;
    RendGraphRecipeBuilderVK& operator=(const RendGraphRecipeBuilderVK&) = default;
    RendGraphRecipeBuilderVK& operator=(RendGraphRecipeBuilderVK&&) noexcept = default;

    /**
     * @brief Build the graph descriptor of a recipe.
     * @param p_recipe The recipe, deserialized strictly as RendGraphRecipeVK.
     * @param p_imports The resources the recipe imports.
     * @param p_render_data Render data whose allocator owns the descriptor; it must outlive the descriptor and its graph.
     * @return The descriptor, for a rebuild callback to return (see RendererVK::register_render_graph()).
     * @throws std::runtime_error If the recipe is malformed; a declared import (resource or task) is not supplied or a
     * supplied one is not declared; a resource id is empty, repeated, or sets other than exactly one kind; a task id is
     * empty or repeated, a task sets no type or an imported task sets a type, params, aliases, trigger, or toggle, its aliases are
     * not a map of strings, or it connects to an unknown task; or a task cannot be added (see
     * RenderTaskRegistryVK::add_task()). Messages name the recipe location, e.g. `tasks[2] 'gbuffer'`.
     */
    RendGraphDescVK build(const DataValue& p_recipe, const RendGraphRecipeImportsVK& p_imports, const RenderContextVK* p_render_data) const;
};

} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_RENDER_TASK_REGISTRY_VK_HH
#define WBE_FILE_RENDER_TASK_REGISTRY_VK_HH

#include "core/data/data_value.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <span>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class RenderTaskRegistryVK
 * @brief Creates render-task factories from a task type name and DataValue parameters.
 *
 * The registry is generated from every class annotated with WBE_META(WBE_RENDER_TASK). A task type is registered under its
 * class name and under each WBE_SERIALIZATION_ALIASES entry; a name used twice fails the build. Annotating the class
 * WBE_RENDER_TASK(<method>) instead also generates the recipe constructor Graph:<method>() of
 * render_graph_utils.render_graph, which creates the task type of the first alias. Every such class must declare
 *
 * @code
 * WBE_META(WBE_REFLECT)
 * static Unique<IRenderTaskVK> create(const RenderContextVK* p_render_data, const TParams& p_params,
 *     const RendGraphContextVK& p_context);
 * @endcode
 *
 * where TParams is a copyable reflected struct (for example WBE_SERIALIZABLE_STATIC). A missing create() or a different
 * signature fails the build. A task type that needs graph resources or aliases besides its own may also declare
 *
 * @code
 * static void prepare_graph(RendGraphDescVK& p_graph, RendTaskDescVK& p_task, const TParams& p_params,
 *     const RenderContextVK* p_render_data);
 * @endcode
 *
 * which add_task() calls before adding the task, for example to import the resources the task reads. The registry has no
 * state; create_factory() may be called from any thread, and add_task() wherever the task type's prepare_graph() may run.
 */
class RenderTaskRegistryVK final {
public:
    RenderTaskRegistryVK() = delete;
    ~RenderTaskRegistryVK() = delete;
    RenderTaskRegistryVK(const RenderTaskRegistryVK&) = delete;
    RenderTaskRegistryVK(RenderTaskRegistryVK&&) noexcept = delete;
    RenderTaskRegistryVK& operator=(const RenderTaskRegistryVK&) = delete;
    RenderTaskRegistryVK& operator=(RenderTaskRegistryVK&&) noexcept = delete;

    /**
     * @brief Create the factory of one render task.
     *
     * The parameters are deserialized into the task type's TParams immediately, so invalid parameters fail here rather than
     * during graph construction. The factory keeps a copy of them and the render-data pointer, and calls the task type's
     * create() every time the render graph evaluates it.
     *
     * @param p_task_type A registered task type name.
     * @param p_params The task parameters, deserialized through SerializableSD into the task type's TParams.
     * @param p_render_data Render data passed to create(); it must outlive the factory.
     * @return The factory, suitable for RendTaskDescVK::factory.
     * @throws std::runtime_error If the task type is unknown, p_render_data is null, or the parameters cannot be
     * deserialized.
     */
    static RendTaskFactoryVK create_factory(std::string_view p_task_type, const DataValue& p_params, const RenderContextVK* p_render_data);

    /**
     * @brief Add one render task to a graph descriptor.
     *
     * Deserializes the parameters as create_factory() does, calls the task type's prepare_graph() on the new task descriptor
     * if it declares one, and adds the descriptor to p_graph.
     *
     * @param p_graph The graph descriptor to add the task to.
     * @param p_task_id The task id; unique in p_graph.
     * @param p_task_type A registered task type name.
     * @param p_params The task parameters.
     * @param p_render_data Render data passed to create() and prepare_graph(); it must outlive the graph.
     * @return The added task descriptor, for aliases and edges; invalidated when another task is added.
     * @throws std::runtime_error Under the conditions of create_factory(), if p_task_id is empty or already used in p_graph,
     * or whatever prepare_graph() throws.
     */
    static RendTaskDescVK& add_task(RendGraphDescVK& p_graph,
        std::string_view p_task_id,
        std::string_view p_task_type,
        const DataValue& p_params,
        const RenderContextVK* p_render_data);

    /**
     * @brief Check whether a task type name is registered.
     */
    static bool contains(std::string_view p_task_type);

    /**
     * @brief Get every registered task type name: class names and aliases.
     */
    static std::span<const std::string_view> get_task_types();
};

} // namespace WhiteBirdEngine

#endif

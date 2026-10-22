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
#ifndef WBE_FILE_RENDER_TASK_RESOURCE_MAP_HH
#define WBE_FILE_RENDER_TASK_RESOURCE_MAP_HH

#include "core/allocator/stl_allocator.hh"
#include "function/render/render_defs.hh"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class RenderTaskResourceMap
 * @brief Map from the resource identifiers one render task uses to graph resource identifiers.
 *
 * A task refers to resources by task-local identifiers: reflected shader resource ids, render targets such as
 * `render_target_colors[0]`, and ids passed to its constructor. RendTaskDescVK owns one map per task; the render graph
 * resolves every identifier the task binds or reports as an access through it. Unmapped identifiers resolve to themselves.
 *
 * Bindings of the enclosing scopes, such as the render graph subgraphs that contain the task, map the result further; see
 * enclose(). They are not the task's own aliases, so they need not be used.
 *
 * The map is copyable and movable and uses an allocator that must outlive every copy. Resolution records which entries were
 * used so that the graph can reject entries that the task never uses.
 */
class RenderTaskResourceMap {
public:
    /**
     * @class ScopeBinding
     * @brief Maps a resource identifier as a scope names it to the identifier the enclosing scope names it by.
     */
    struct ScopeBinding {
        /** The identifier inside the scope. */
        std::string_view source;
        /** The identifier outside the scope. */
        std::string_view target;
    };

    RenderTaskResourceMap() = delete;
    /**
     * @brief Create an empty mapping using an allocator that outlives this configuration.
     */
    explicit RenderTaskResourceMap(RenderAllocator* p_allocator);
    ~RenderTaskResourceMap() = default;
    RenderTaskResourceMap(const RenderTaskResourceMap&) = default;
    RenderTaskResourceMap(RenderTaskResourceMap&&) noexcept = default;
    RenderTaskResourceMap& operator=(const RenderTaskResourceMap&) = default;
    RenderTaskResourceMap& operator=(RenderTaskResourceMap&&) noexcept = default;

    /**
     * @brief Map a task-local resource identifier to a graph resource identifier.
     * @param p_task_resource_id Identifier used by the task.
     * @param p_graph_resource_id Identifier of the graph resource.
     * @return *this.
     * @throws std::runtime_error If either identifier is empty or the task-local identifier is already mapped.
     */
    RenderTaskResourceMap& bind(std::string_view p_task_resource_id, std::string_view p_graph_resource_id);

    /**
     * @brief Enclose the task in a scope that renames resource identifiers.
     *
     * An identifier resolves through the task's own bindings, then through every scope from the innermost to the outermost.
     * Enclosing composes p_scope with the scopes the task is already in, so scopes are applied from the inner one out.
     *
     * @param p_scope Bindings of the scope; each source appears once. They are copied.
     * @return *this.
     * @throws std::runtime_error If an identifier is empty or a source appears twice.
     */
    RenderTaskResourceMap& enclose(std::span<const ScopeBinding> p_scope);

    /**
     * @brief Resolve a task-local identifier and record the matching entry as used.
     * @param p_task_resource_id Identifier used by the task.
     * @return The graph identifier after the task's bindings and its scopes; p_task_resource_id when neither maps it.
     */
    std::string_view resolve(std::string_view p_task_resource_id) const;

    /**
     * @brief Require that every entry was resolved at least once.
     * @param p_task_id Task identifier used in the error message.
     * @throws std::runtime_error If an entry was never resolved.
     */
    void validate_all_used(std::string_view p_task_id) const;

private:
    struct Entry {
        std::string source;
        std::string target;
    };
    Vector<Entry, RenderAllocator> entries;
    // The composition of the enclosing scopes, applied after entries.
    Vector<Entry, RenderAllocator> scope;
    // Resolution bookkeeping, parallel to entries.
    mutable Vector<uint8_t, RenderAllocator> used;
};

} // namespace WhiteBirdEngine

#endif

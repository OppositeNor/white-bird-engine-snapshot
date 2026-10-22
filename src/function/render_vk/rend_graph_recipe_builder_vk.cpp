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
#include "function/render_vk/rend_graph_recipe_builder_vk.hh"
#include "core/data/data_value.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/rend_graph_recipe_params_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_task_registry_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_toggle_vk.hh"
#include "function/render_vk/rt_triggered_vk.hh"
#include "generated/serializables_sd.gen.hh"
#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

namespace {

// What every level of a recipe shares: its subgraphs are built into descriptors of their own.
struct BuildState {
    const RendGraphRecipeImportsVK& imports;
    const RenderContextVK* render_data;
    // The supplied imports and tasks some level of the recipe declares.
    std::set<std::string, std::less<>> declared_imports;
    std::set<std::string, std::less<>> imported_tasks;
};

std::string resource_location(std::string_view p_scope, size_t p_index, std::string_view p_id) {
    return std::format("{}resources[{}] '{}'", p_scope, p_index, p_id);
}

std::string task_location(std::string_view p_scope, size_t p_index, std::string_view p_id) {
    return std::format("{}tasks[{}] '{}'", p_scope, p_index, p_id);
}

void bind_imports(RendGraphDescVK& p_graph, const RendGraphRecipeVK& p_recipe, BuildState& p_state, std::string_view p_scope) {
    std::set<std::string, std::less<>> declared;
    for (size_t i = 0; i < p_recipe.imports.size(); ++i) {
        const std::string& name = p_recipe.imports[i];
        if (!declared.insert(name).second) {
            throw std::runtime_error(
                std::format("Failed to build render graph recipe: {}imports[{}] '{}' is listed more than once.", p_scope, i, name));
        }
        const IBindableRenderResourceVK* resource = p_state.imports.find(name);
        if (resource == nullptr) {
            throw std::runtime_error(
                std::format("Failed to build render graph recipe: {}imports[{}] '{}' is not supplied.", p_scope, i, name));
        }
        p_graph.add_resource(name, *resource);
        p_state.declared_imports.insert(name);
    }
}

void declare_resources(RendGraphDescVK& p_graph, const RendGraphRecipeVK& p_recipe, std::string_view p_scope) {
    std::set<std::string, std::less<>> ids;
    for (size_t i = 0; i < p_recipe.resources.size(); ++i) {
        const RendRecipeResourceVK& resource = p_recipe.resources[i];
        if (resource.id.empty()) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {}resources[{}] has an empty id.", p_scope, i));
        }
        if (!ids.insert(resource.id).second || p_graph.contains_resource(resource.id)) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {} repeats a resource or import id.",
                resource_location(p_scope, i, resource.id)));
        }
        const int kind_count = static_cast<int>(resource.buffer.has_value()) + static_cast<int>(resource.image.has_value()) +
                               static_cast<int>(resource.image_view.has_value()) + static_cast<int>(resource.sampler.has_value());
        if (kind_count != 1) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {} must set exactly one of buffer, image, image_view, and sampler.",
                resource_location(p_scope, i, resource.id)));
        }
        if (resource.buffer.has_value()) {
            p_graph.add_buffer(resource.id, [params = *resource.buffer](const RendGraphContextVK& /*p_context*/) {
                return params.to_desc();
            });
        } else if (resource.image.has_value()) {
            p_graph.add_image(resource.id, [params = *resource.image](const RendGraphContextVK& p_context) {
                return params.to_desc(p_context);
            });
        } else if (resource.image_view.has_value()) {
            p_graph.add_image_view(
                resource.id, resource.image_view->image, [params = resource.image_view->view](const RendGraphContextVK& /*p_context*/) {
                    return params.to_desc();
                });
        } else {
            p_graph.add_sampler(resource.id, [params = *resource.sampler](const RendGraphContextVK& /*p_context*/) {
                return params.to_desc();
            });
        }
    }
}

// Whether a recipe map field is set; an empty Lua table reads as an empty list.
bool sets_map(const DataValue& p_value) {
    return !p_value.is_nil() && !(p_value.get_kind() == DataValueKind::LIST && p_value.as_list().empty());
}

// Call p_apply with every entry of a recipe map from ids to resource id strings.
template <typename TApply>
void for_each_resource_entry(
    const DataValue& p_map, std::string_view p_field, std::string_view p_entry, std::string_view p_location, TApply&& p_apply) {
    if (!sets_map(p_map)) {
        return;
    }
    if (p_map.get_kind() != DataValueKind::MAP) {
        throw std::runtime_error(std::format("Failed to build render graph recipe: {} {} must be a map.", p_location, p_field));
    }
    for (const auto& [key, value] : p_map.as_map()) {
        if (!value.is_string()) {
            throw std::runtime_error(std::format(
                "Failed to build render graph recipe: {} {} '{}' must map to a graph resource id string.", p_location, p_entry, key));
        }
        p_apply(key, value.as_string());
    }
}

void apply_aliases(RendTaskDescVK& p_task, const DataValue& p_aliases, std::string_view p_location) {
    for_each_resource_entry(p_aliases, "aliases", "alias", p_location, [&p_task](const std::string& p_task_resource_id, const std::string& p_graph_resource_id) {
        p_task.alias(p_task_resource_id, p_graph_resource_id);
    });
}

// Wrap a factory as the recipe task's trigger and toggle select; the toggle wraps outermost, so it gates a triggered task too.
RendTaskFactoryVK wrap_gating(
    RendTaskFactoryVK p_factory, const RendRecipeTaskVK& p_recipe_task, const RenderContextVK* p_render_data, std::string_view p_location) {
    if (p_recipe_task.trigger.has_value()) {
        if (p_recipe_task.trigger->event.empty()) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {} trigger event is empty.", p_location));
        }
        p_factory = RTTriggeredVK::wrap_factory(std::move(p_factory), p_recipe_task.trigger->event, p_render_data, p_recipe_task.trigger->activate);
    }
    if (p_recipe_task.toggle.has_value()) {
        if (p_recipe_task.toggle->event.empty()) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {} toggle event is empty.", p_location));
        }
        p_factory = RTToggleVK::wrap_factory(std::move(p_factory), p_recipe_task.toggle->event, p_render_data, p_recipe_task.toggle->enabled);
    }
    return p_factory;
}

// Copy the task the caller supplies for an imported recipe task into the graph.
void add_imported_task(RendGraphDescVK& p_graph, const RendRecipeTaskVK& p_recipe_task, BuildState& p_state, std::string_view p_location) {
    if (!p_recipe_task.type.empty()) {
        throw std::runtime_error(std::format("Failed to build render graph recipe: {} is imported, so it cannot set a type.", p_location));
    }
    if (!p_recipe_task.params.is_nil()) {
        throw std::runtime_error(std::format("Failed to build render graph recipe: {} is imported, so it cannot set params.", p_location));
    }
    if (sets_map(p_recipe_task.aliases)) {
        throw std::runtime_error(std::format("Failed to build render graph recipe: {} is imported, so it cannot set aliases.", p_location));
    }
    if (p_recipe_task.trigger.has_value()) {
        throw std::runtime_error(
            std::format("Failed to build render graph recipe: {} is imported, so it cannot set a trigger.", p_location));
    }
    if (p_recipe_task.toggle.has_value()) {
        throw std::runtime_error(
            std::format("Failed to build render graph recipe: {} is imported, so it cannot set a toggle.", p_location));
    }
    const RendTaskDescVK* task = p_state.imports.find_task(p_recipe_task.id);
    if (task == nullptr) {
        throw std::runtime_error(std::format("Failed to build render graph recipe: {} is imported but not supplied.", p_location));
    }
    if (!p_state.imported_tasks.insert(p_recipe_task.id).second) {
        throw std::runtime_error(std::format("Failed to build render graph recipe: {} imports a supplied task another task imports.", p_location));
    }
    p_graph.add_task(*task);
}

void build_level(RendGraphDescVK& p_graph, const RendGraphRecipeVK& p_recipe, BuildState& p_state, std::string_view p_scope);

// Build a subgraph task's nested recipe and add it to the graph under the task's id.
void add_subgraph_task(RendGraphDescVK& p_graph, const RendRecipeTaskVK& p_recipe_task, BuildState& p_state, std::string_view p_location) {
    if (p_recipe_task.import || !p_recipe_task.type.empty() || !p_recipe_task.params.is_nil() || sets_map(p_recipe_task.aliases)) {
        throw std::runtime_error(std::format(
            "Failed to build render graph recipe: {} is a subgraph, so it cannot set a type, params, aliases, or import.", p_location));
    }
    RendGraphRecipeVK recipe;
    SerializableSD<RendGraphRecipeVK>::deserialize(p_recipe_task.subgraph, recipe);
    RendSubgraphDescVK subgraph(p_graph.allocator);
    for (const std::string& port : recipe.ports) {
        subgraph.add_port(port);
    }
    build_level(subgraph.graph, recipe, p_state, std::format("{} subgraph ", p_location));
    for (RendTaskDescVK& task : subgraph.graph.tasks) {
        task.factory = wrap_gating(std::move(task.factory), p_recipe_task, p_state.render_data, p_location);
    }
    std::vector<std::pair<std::string, std::string>> bound;
    for_each_resource_entry(p_recipe_task.ports, "ports", "port", p_location, [&bound](const std::string& p_port, const std::string& p_resource) {
        bound.emplace_back(p_port, p_resource);
    });
    std::vector<RendSubgraphPortBindingVK> bindings;
    bindings.reserve(bound.size());
    for (const auto& [port, resource] : bound) {
        bindings.push_back({.port = port, .resource = resource});
    }
    p_graph.add_subgraph(p_recipe_task.id, subgraph, bindings);
}

void add_tasks(RendGraphDescVK& p_graph, const RendGraphRecipeVK& p_recipe, BuildState& p_state, std::string_view p_scope) {
    std::set<std::string, std::less<>> ids;
    std::set<std::string, std::less<>> subgraph_ids;
    for (size_t i = 0; i < p_recipe.tasks.size(); ++i) {
        const RendRecipeTaskVK& recipe_task = p_recipe.tasks[i];
        const std::string location = task_location(p_scope, i, recipe_task.id);
        if (recipe_task.id.empty()) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {}tasks[{}] has an empty id.", p_scope, i));
        }
        if (!ids.insert(recipe_task.id).second) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {} repeats a task id.", location));
        }
        if (!recipe_task.subgraph.is_nil()) {
            add_subgraph_task(p_graph, recipe_task, p_state, location);
            subgraph_ids.insert(recipe_task.id);
            continue;
        }
        if (sets_map(recipe_task.ports)) {
            throw std::runtime_error(std::format("Failed to build render graph recipe: {} sets ports but is not a subgraph.", location));
        }
        if (recipe_task.import) {
            add_imported_task(p_graph, recipe_task, p_state, location);
            continue;
        }
        if (recipe_task.type.empty()) {
            throw std::runtime_error(
                std::format("Failed to build render graph recipe: {} sets no type; set one, or set import for a supplied task.", location));
        }
        RendTaskDescVK& task = RenderTaskRegistryVK::add_task(p_graph, recipe_task.id, recipe_task.type, recipe_task.params, p_state.render_data);
        apply_aliases(task, recipe_task.aliases, location);
        task.factory = wrap_gating(std::move(task.factory), recipe_task, p_state.render_data, location);
    }
    // Edges may point to later tasks, so they are added once every task exists. They may also name a subgraph's task.
    const auto is_task = [&p_graph](std::string_view p_id) {
        return std::ranges::any_of(p_graph.tasks, [p_id](const RendTaskDescVK& p_task) {
            return p_task.task_id == p_id;
        });
    };
    for (size_t i = 0; i < p_recipe.tasks.size(); ++i) {
        const RendRecipeTaskVK& recipe_task = p_recipe.tasks[i];
        for (size_t j = 0; j < recipe_task.connect.size(); ++j) {
            const RendRecipeEdgeVK& edge = recipe_task.connect[j];
            if (!ids.contains(edge.to) && !is_task(edge.to)) {
                throw std::runtime_error(std::format("Failed to build render graph recipe: {} connect[{}] targets the unknown task '{}'.",
                    task_location(p_scope, i, recipe_task.id),
                    j,
                    edge.to));
            }
            if (subgraph_ids.contains(recipe_task.id)) {
                p_graph.get_subgraph(recipe_task.id).connect(edge.to, edge.resource);
            } else {
                p_graph.get_task(recipe_task.id).connect(edge.to, edge.resource);
            }
        }
    }
}

void build_level(RendGraphDescVK& p_graph, const RendGraphRecipeVK& p_recipe, BuildState& p_state, std::string_view p_scope) {
    bind_imports(p_graph, p_recipe, p_state, p_scope);
    declare_resources(p_graph, p_recipe, p_scope);
    add_tasks(p_graph, p_recipe, p_state, p_scope);
}

} // namespace

RendGraphRecipeImportsVK& RendGraphRecipeImportsVK::add(std::string_view p_name, const IBindableRenderResourceVK& p_resource) {
    if (p_name.empty()) {
        throw std::runtime_error("Failed to add render graph recipe import: the name is empty.");
    }
    if (!resources.emplace(std::string(p_name), &p_resource).second) {
        throw std::runtime_error(std::format("Failed to add render graph recipe import: '{}' is already supplied.", p_name));
    }
    return *this;
}

const IBindableRenderResourceVK* RendGraphRecipeImportsVK::find(std::string_view p_name) const {
    auto resource = resources.find(p_name);
    return resource == resources.end() ? nullptr : resource->second;
}

RendGraphRecipeImportsVK& RendGraphRecipeImportsVK::add_task(RendTaskDescVK p_task) {
    if (p_task.task_id.empty()) {
        throw std::runtime_error("Failed to add render graph recipe task import: the task id is empty.");
    }
    const std::string task_id = p_task.task_id;
    if (!tasks.emplace(task_id, std::move(p_task)).second) {
        throw std::runtime_error(std::format("Failed to add render graph recipe task import: '{}' is already supplied.", task_id));
    }
    return *this;
}

const RendTaskDescVK* RendGraphRecipeImportsVK::find_task(std::string_view p_task_id) const {
    auto task = tasks.find(p_task_id);
    return task == tasks.end() ? nullptr : &task->second;
}

std::vector<std::string> RendGraphRecipeImportsVK::get_task_ids() const {
    std::vector<std::string> task_ids;
    task_ids.reserve(tasks.size());
    for (const auto& [task_id, task] : tasks) {
        static_cast<void>(task);
        task_ids.push_back(task_id);
    }
    return task_ids;
}

std::vector<std::string> RendGraphRecipeImportsVK::get_names() const {
    std::vector<std::string> names;
    names.reserve(resources.size());
    for (const auto& [name, resource] : resources) {
        static_cast<void>(resource);
        names.push_back(name);
    }
    return names;
}

RendGraphDescVK RendGraphRecipeBuilderVK::build(
    const DataValue& p_recipe, const RendGraphRecipeImportsVK& p_imports, const RenderContextVK* p_render_data) const {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error("Failed to build render graph recipe: render data is unavailable.");
    }
    RendGraphRecipeVK recipe;
    SerializableSD<RendGraphRecipeVK>::deserialize(p_recipe, recipe);
    if (!recipe.ports.empty()) {
        throw std::runtime_error("Failed to build render graph recipe: only a subgraph's nested recipe declares ports.");
    }
    RendGraphDescVK graph(p_render_data->allocator);
    BuildState state{.imports = p_imports, .render_data = p_render_data, .declared_imports = {}, .imported_tasks = {}};
    build_level(graph, recipe, state, "");
    for (const std::string& name : p_imports.get_names()) {
        if (!state.declared_imports.contains(name)) {
            throw std::runtime_error(
                std::format("Failed to build render graph recipe: the supplied import '{}' is not declared by the recipe.", name));
        }
    }
    for (const std::string& task_id : p_imports.get_task_ids()) {
        if (!state.imported_tasks.contains(task_id)) {
            throw std::runtime_error(std::format(
                "Failed to build render graph recipe: the supplied task '{}' is not imported by the recipe.", task_id));
        }
    }
    return graph;
}

} // namespace WhiteBirdEngine

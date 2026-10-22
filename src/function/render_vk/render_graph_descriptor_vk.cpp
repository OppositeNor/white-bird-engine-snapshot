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
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/i_render_task.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_task_resource_map.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/roc_single_vk.hh"
#include <algorithm>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

// Renames the identifiers of a subgraph being added: task and subgraph ids, and the resource identifiers its scope maps.
class SubgraphScope {
public:
    SubgraphScope(RenderAllocator* p_allocator, std::string_view p_subgraph_id)
        : prefix(std::format("{}.", p_subgraph_id)), local_ids(p_allocator), resource_sources(p_allocator),
          resource_targets(p_allocator) {
    }

    std::string prefixed(std::string_view p_id) const {
        return prefix + std::string(p_id);
    }

    void add_local_id(std::string_view p_id) {
        local_ids.emplace_back(p_id);
    }

    void add_resource(std::string_view p_source, std::string p_target) {
        resource_sources.emplace_back(p_source);
        resource_targets.push_back(std::move(p_target));
    }

    // The id a task or subgraph inside the scope has outside it.
    std::string task_id(std::string_view p_id, std::string_view p_subgraph_id) const {
        if (std::ranges::find(local_ids, p_id) == local_ids.end()) {
            throw std::runtime_error(std::format(
                "Failed to add render graph subgraph '{}': an edge inside it connects to '{}', which is not in the subgraph.",
                p_subgraph_id,
                p_id));
        }
        return prefixed(p_id);
    }

    // The identifier a resource named inside the scope has outside it.
    std::string resource_id(std::string_view p_id) const {
        for (size_t i = 0; i < resource_sources.size(); ++i) {
            if (resource_sources[i] == p_id) {
                return resource_targets[i];
            }
        }
        return std::string(p_id);
    }

    Vector<RenderTaskResourceMap::ScopeBinding, RenderAllocator> bindings(RenderAllocator* p_allocator) const {
        Vector<RenderTaskResourceMap::ScopeBinding, RenderAllocator> result(p_allocator);
        for (size_t i = 0; i < resource_sources.size(); ++i) {
            result.push_back({.source = resource_sources[i], .target = resource_targets[i]});
        }
        return result;
    }

private:
    std::string prefix;
    Vector<std::string, RenderAllocator> local_ids;
    Vector<std::string, RenderAllocator> resource_sources;
    Vector<std::string, RenderAllocator> resource_targets;
};

// Whether two imports bind the same resource. Importing a render object wraps it in a new single-object container each
// time, so those match by the object they hold.
bool is_same_import(const RendResVK& p_left, const RendResVK& p_right) {
    if (p_left.res_type_id != p_right.res_type_id || p_left.resource_list != p_right.resource_list) {
        return false;
    }
    if (p_left.container == p_right.container) {
        return true;
    }
    if (p_left.container == MEM_NULL || p_right.container == MEM_NULL) {
        return false;
    }
    const Ref<ROCSingleVK> left = p_left.container.dynamic_cast_ref<ROCSingleVK>();
    const Ref<ROCSingleVK> right = p_right.container.dynamic_cast_ref<ROCSingleVK>();
    return left != MEM_NULL && right != MEM_NULL && left->get().get() == right->get().get();
}

} // namespace

RendTaskDescVK::RendTaskDescVK(RenderAllocator* p_allocator) : next(p_allocator), resource_map(p_allocator) {
}

RendTaskDescVK::RendTaskDescVK(RenderAllocator* p_allocator, std::string_view p_task_id, RendTaskFactoryVK p_factory)
    : task_id(p_task_id), next(p_allocator), resource_map(p_allocator), factory(std::move(p_factory)) {
    if (factory == nullptr) {
        throw std::runtime_error("Failed to construct render task descriptor: task factory is null.");
    }
}

RendTaskDescVK& RendTaskDescVK::connect(std::string_view p_next_task_id, std::string_view p_resource_id) {
    next.push_back({
        .next_task_id = std::string(p_next_task_id),
        .res_id = std::string(p_resource_id),
    });
    return *this;
}

RendTaskDescVK& RendTaskDescVK::alias(std::string_view p_task_resource_id, std::string_view p_graph_resource_id) {
    resource_map.bind(p_task_resource_id, p_graph_resource_id);
    return *this;
}

RendGraphDescVK& RendGraphDescVK::add_task(RendTaskDescVK p_task_desc) {
    tasks.emplace_back(std::move(p_task_desc));
    return *this;
}

RendGraphDescVK& RendGraphDescVK::add_task(std::string_view p_task_id, RendTaskFactoryVK p_factory) {
    tasks.emplace_back(allocator, p_task_id, std::move(p_factory));
    return *this;
}

RendTaskDescVK& RendGraphDescVK::get_task(std::string_view p_task_id) {
    for (RendTaskDescVK& task : tasks) {
        if (task.task_id == p_task_id) {
            return task;
        }
    }
    throw std::runtime_error(std::format("Failed to get render graph task: no task has id '{}'.", p_task_id));
}

RendSubgraphVK::RendSubgraphVK(RenderAllocator* p_allocator, std::string_view p_subgraph_id)
    : subgraph_id(p_subgraph_id), members(p_allocator), next(p_allocator) {
}

RendSubgraphVK& RendSubgraphVK::connect(std::string_view p_next_task_id, std::string_view p_resource_id) {
    next.push_back({.next_task_id = RendTaskID(p_next_task_id), .res_id = std::string(p_resource_id)});
    return *this;
}

RendSubgraphDescVK& RendSubgraphDescVK::add_port(std::string_view p_port) {
    if (p_port.empty()) {
        throw std::runtime_error("Failed to add render graph subgraph port: the port is empty.");
    }
    if (std::ranges::find(ports, p_port) != ports.end()) {
        throw std::runtime_error(std::format("Failed to add render graph subgraph port: port '{}' is declared twice.", p_port));
    }
    ports.emplace_back(p_port);
    return *this;
}

RendSubgraphVK& RendGraphDescVK::add_subgraph(
    std::string_view p_subgraph_id, const RendSubgraphDescVK& p_subgraph, std::span<const RendSubgraphPortBindingVK> p_ports) {
    if (p_subgraph_id.empty()) {
        throw std::runtime_error("Failed to add render graph subgraph: the subgraph id is empty.");
    }
    const auto has_task = std::ranges::any_of(tasks, [p_subgraph_id](const RendTaskDescVK& p_task) {
        return p_task.task_id == p_subgraph_id;
    });
    const auto has_subgraph = std::ranges::any_of(subgraphs, [p_subgraph_id](const RendSubgraphVK& p_record) {
        return p_record.subgraph_id == p_subgraph_id;
    });
    if (has_task || has_subgraph) {
        throw std::runtime_error(std::format("Failed to add render graph subgraph '{}': a task or subgraph already has the id.", p_subgraph_id));
    }
    const RendGraphDescVK& inner = p_subgraph.graph;
    SubgraphScope scope(allocator, p_subgraph_id);

    // Ports name resources of this graph; they must not shadow what the subgraph declares or imports.
    for (const RendSubgraphPortBindingVK& binding : p_ports) {
        if (std::ranges::find(p_subgraph.ports, binding.port) == p_subgraph.ports.end()) {
            throw std::runtime_error(std::format("Failed to add render graph subgraph '{}': it has no port '{}'.", p_subgraph_id, binding.port));
        }
        if (binding.resource.empty()) {
            throw std::runtime_error(
                std::format("Failed to add render graph subgraph '{}': port '{}' is bound to an empty resource id.", p_subgraph_id, binding.port));
        }
    }
    for (const std::string& port : p_subgraph.ports) {
        const auto count = std::ranges::count_if(p_ports, [&port](const RendSubgraphPortBindingVK& p_binding) {
            return p_binding.port == port;
        });
        if (count != 1) {
            throw std::runtime_error(std::format("Failed to add render graph subgraph '{}': port '{}' is bound {} times instead of once.",
                p_subgraph_id,
                port,
                count));
        }
        if (inner.contains_resource(port)) {
            throw std::runtime_error(std::format(
                "Failed to add render graph subgraph '{}': port '{}' names a resource the subgraph declares or imports.", p_subgraph_id, port));
        }
    }
    for (const RendSubgraphPortBindingVK& binding : p_ports) {
        scope.add_resource(binding.port, std::string(binding.resource));
    }
    const auto add_owned = [&scope](const auto& p_declarations) {
        for (const auto& declaration : p_declarations) {
            scope.add_resource(declaration.res_id, scope.prefixed(declaration.res_id));
        }
    };
    add_owned(inner.buffers);
    add_owned(inner.images);
    add_owned(inner.image_views);
    add_owned(inner.samplers);
    for (const RendTaskDescVK& task : inner.tasks) {
        scope.add_local_id(task.task_id);
    }
    for (const RendSubgraphVK& nested : inner.subgraphs) {
        scope.add_local_id(nested.subgraph_id);
    }
    const auto rename_edges = [&scope, p_subgraph_id](Vector<RendResEdgeDescVK, RenderAllocator>& p_edges) {
        for (RendResEdgeDescVK& edge : p_edges) {
            edge.next_task_id = scope.task_id(edge.next_task_id, p_subgraph_id);
            edge.res_id = scope.resource_id(edge.res_id);
        }
    };

    // The subgraph's imports are imports of this graph.
    for (const RendResVK& resource : inner.resources) {
        const auto existing = std::ranges::find_if(resources, [&resource](const RendResVK& p_resource) {
            return p_resource.res_id == resource.res_id;
        });
        if (existing == resources.end()) {
            if (contains_resource(resource.res_id)) {
                throw std::runtime_error(std::format(
                    "Failed to add render graph subgraph '{}': it imports '{}', which the graph declares.", p_subgraph_id, resource.res_id));
            }
            resources.push_back(resource);
            continue;
        }
        if (!is_same_import(*existing, resource)) {
            throw std::runtime_error(std::format(
                "Failed to add render graph subgraph '{}': it imports '{}', which the graph imports as another resource.", p_subgraph_id, resource.res_id));
        }
    }
    for (const RendBufferDeclVK& buffer : inner.buffers) {
        buffers.push_back({.res_id = scope.prefixed(buffer.res_id), .factory = buffer.factory});
    }
    for (const RendImageDeclVK& image : inner.images) {
        images.push_back({.res_id = scope.prefixed(image.res_id), .factory = image.factory});
    }
    for (const RendImageViewDeclVK& view : inner.image_views) {
        image_views.push_back({.res_id = scope.prefixed(view.res_id), .image_id = scope.resource_id(view.image_id), .factory = view.factory});
    }
    for (const RendSamplerDeclVK& sampler : inner.samplers) {
        samplers.push_back({.res_id = scope.prefixed(sampler.res_id), .factory = sampler.factory});
    }

    RendSubgraphVK record(allocator, p_subgraph_id);
    const Vector<RenderTaskResourceMap::ScopeBinding, RenderAllocator> bindings = scope.bindings(allocator);
    for (const RendTaskDescVK& inner_task : inner.tasks) {
        RendTaskDescVK task = inner_task;
        task.task_id = scope.prefixed(inner_task.task_id);
        rename_edges(task.next);
        task.resource_map.enclose(bindings);
        record.members.push_back(task.task_id);
        tasks.push_back(std::move(task));
    }
    for (const RendSubgraphVK& inner_record : inner.subgraphs) {
        RendSubgraphVK nested = inner_record;
        nested.subgraph_id = scope.prefixed(inner_record.subgraph_id);
        for (RendTaskID& member : nested.members) {
            member = scope.prefixed(member);
        }
        rename_edges(nested.next);
        subgraphs.push_back(std::move(nested));
    }
    subgraphs.push_back(std::move(record));
    return subgraphs.back();
}

RendSubgraphVK& RendGraphDescVK::get_subgraph(std::string_view p_subgraph_id) {
    for (RendSubgraphVK& record : subgraphs) {
        if (record.subgraph_id == p_subgraph_id) {
            return record;
        }
    }
    throw std::runtime_error(std::format("Failed to get render graph subgraph: no subgraph has id '{}'.", p_subgraph_id));
}

RendGraphDescVK& RendGraphDescVK::add_resource(RendResVK p_resource) {
    if (p_resource.container != MEM_NULL) {
        RendResTID concrete_type_id = p_resource.container->get_resource_type_id();
        if (concrete_type_id == 0) {
            throw std::runtime_error("Failed to add render graph resource: container has no concrete resource type.");
        }
        if (p_resource.res_type_id != 0 && p_resource.res_type_id != concrete_type_id) {
            throw std::runtime_error("Failed to add render graph resource: container type does not match the declared type.");
        }
        p_resource.res_type_id = concrete_type_id;
    }
    resources.emplace_back(std::move(p_resource));
    return *this;
}

RendGraphDescVK& RendGraphDescVK::add_resource(std::string_view p_resource_id, const IBindableRenderResourceVK& p_resource) {
    p_resource.bind(*this, p_resource_id);
    return *this;
}

bool RendGraphDescVK::contains_resource(std::string_view p_resource_id) const {
    const auto has_id = [p_resource_id](const auto& p_entries) {
        return std::ranges::any_of(p_entries, [p_resource_id](const auto& p_entry) {
            return p_entry.res_id == p_resource_id;
        });
    };
    return has_id(resources) || has_id(buffers) || has_id(images) || has_id(image_views) || has_id(samplers);
}

RendGraphDescVK& RendGraphDescVK::add_buffer(std::string_view p_resource_id, RendBufferFactoryVK p_factory) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("Failed to add render graph buffer: resource id is empty.");
    }
    if (p_factory == nullptr) {
        throw std::runtime_error("Failed to add render graph buffer: factory is null.");
    }
    buffers.push_back({.res_id = std::string(p_resource_id), .factory = std::move(p_factory)});
    return *this;
}

RendGraphDescVK& RendGraphDescVK::add_image(std::string_view p_resource_id, RendImageFactoryVK p_factory) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("Failed to add render graph image: resource id is empty.");
    }
    if (p_factory == nullptr) {
        throw std::runtime_error("Failed to add render graph image: factory is null.");
    }
    images.push_back({.res_id = std::string(p_resource_id), .factory = std::move(p_factory)});
    return *this;
}

RendGraphDescVK& RendGraphDescVK::add_image_view(
    std::string_view p_resource_id, std::string_view p_image_id, RendImageViewFactoryVK p_factory) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("Failed to add render graph image view: resource id is empty.");
    }
    if (p_image_id.empty()) {
        throw std::runtime_error("Failed to add render graph image view: image id is empty.");
    }
    if (p_factory == nullptr) {
        p_factory = [](const RendGraphContextVK& /*p_context*/) {
            return RendImageViewDescVK{};
        };
    }
    image_views.push_back({.res_id = std::string(p_resource_id), .image_id = std::string(p_image_id), .factory = std::move(p_factory)});
    return *this;
}

RendGraphDescVK& RendGraphDescVK::add_sampler(std::string_view p_resource_id, RendSamplerFactoryVK p_factory) {
    if (p_resource_id.empty()) {
        throw std::runtime_error("Failed to add render graph sampler: resource id is empty.");
    }
    if (p_factory == nullptr) {
        p_factory = [](const RendGraphContextVK& /*p_context*/) {
            return RendSamplerDescVK{};
        };
    }
    samplers.push_back({.res_id = std::string(p_resource_id), .factory = std::move(p_factory)});
    return *this;
}

bool RendBufferDescVK::operator==(const RendBufferDescVK& p_other) const {
    return usage == p_other.usage && additional_usage == p_other.additional_usage && size == p_other.size &&
           domain == p_other.domain && per_frame == p_other.per_frame && initial_data == p_other.initial_data;
}

bool RendImageDescVK::operator==(const RendImageDescVK& p_other) const {
    return usage == p_other.usage && additional_usage == p_other.additional_usage && format == p_other.format &&
           extent.width == p_other.extent.width && extent.height == p_other.extent.height &&
           extent.depth == p_other.extent.depth && image_type == p_other.image_type && mip_levels == p_other.mip_levels &&
           layers == p_other.layers && per_frame == p_other.per_frame;
}

bool RendImageViewDescVK::operator==(const RendImageViewDescVK& p_other) const {
    return view_type == p_other.view_type && format == p_other.format && aspect_mask == p_other.aspect_mask &&
           base_mip_level == p_other.base_mip_level && mip_level_count == p_other.mip_level_count &&
           base_array_layer == p_other.base_array_layer && layer_count == p_other.layer_count;
}

bool RendSamplerDescVK::operator==(const RendSamplerDescVK& p_other) const {
    return min_filter == p_other.min_filter && mag_filter == p_other.mag_filter && mipmap_mode == p_other.mipmap_mode &&
           address_mode_u == p_other.address_mode_u && address_mode_v == p_other.address_mode_v &&
           address_mode_w == p_other.address_mode_w && mip_lod_bias == p_other.mip_lod_bias && max_anisotropy == p_other.max_anisotropy &&
           compare_op == p_other.compare_op && min_lod == p_other.min_lod && max_lod == p_other.max_lod &&
           border_color == p_other.border_color && unnormalized_coordinates == p_other.unnormalized_coordinates;
}

} // namespace WhiteBirdEngine

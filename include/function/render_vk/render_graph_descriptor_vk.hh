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
#ifndef WBE_FILE_RENDER_GRAPH_DESCRIPTOR_VK_HH
#define WBE_FILE_RENDER_GRAPH_DESCRIPTOR_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/function_utils.hh"
#include "function/render/i_render_task.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_task_resource_map.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RendResPrimaryUsageVK
 * @brief Primary usage of a resource allocated by the render graph; it selects the Vulkan usage flags of the resource.
 *
 * Reflected; the lowercase aliases are the names used in render graph recipes.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) RendResPrimaryUsageVK : uint32_t {
    /** Image sampled by shaders; its content is written by transfers. Sampled and transfer-destination usage. */
    IMAGE_READ_ONLY WBE_META(WBE_SERIALIZATION_ALIASES(image_read_only)),
    /** Image read and written by shaders. Storage and sampled usage. */
    IMAGE_READ_WRITE WBE_META(WBE_SERIALIZATION_ALIASES(image_read_write)),
    /** Color attachment that is later sampled. Color-attachment and sampled usage. */
    IMAGE_RENDER_TARGET WBE_META(WBE_SERIALIZATION_ALIASES(image_render_target)),
    /** Depth/stencil attachment that is later sampled. Depth-stencil-attachment and sampled usage. */
    IMAGE_DEPTH_TARGET WBE_META(WBE_SERIALIZATION_ALIASES(image_depth_target)),
    /** Uniform buffer. */
    BUFFER_UNIFORM WBE_META(WBE_SERIALIZATION_ALIASES(buffer_uniform)),
    /** Storage buffer read or written by shaders. */
    BUFFER_STORAGE WBE_META(WBE_SERIALIZATION_ALIASES(buffer_storage)),
    /** Indirect command buffer written by shaders. Indirect and storage usage. */
    BUFFER_INDIRECT WBE_META(WBE_SERIALIZATION_ALIASES(buffer_indirect)),
    /** Vertex buffer. */
    BUFFER_VERTEX WBE_META(WBE_SERIALIZATION_ALIASES(buffer_vertex)),
    /** Index buffer. */
    BUFFER_INDEX WBE_META(WBE_SERIALIZATION_ALIASES(buffer_index)),
};

/**
 * @class RendBufferDescVK
 * @brief Configuration of a buffer allocated by the render graph during graph compilation.
 */
struct RendBufferDescVK {
    /** Primary usage; must be one of the BUFFER_* usages. */
    RendResPrimaryUsageVK usage = RendResPrimaryUsageVK::BUFFER_STORAGE;
    /** Vulkan usage flags added to the flags selected by the primary usage. */
    VkBufferUsageFlags additional_usage = 0;
    /** Buffer size in bytes. Zero selects the size of initial_data. */
    size_t size = 0;
    /** Memory domain where the buffer is allocated. */
    DeviceDomain domain = DeviceDomain::DEVICE;
    /** Whether one buffer is allocated per frame in flight instead of one for the whole graph. */
    bool per_frame = false;
    /** Bytes uploaded into every allocated buffer during graph compilation. */
    std::vector<uint8_t> initial_data;

    /**
     * @brief Compare two configurations.
     * @param p_other The configuration to compare with.
     * @return True if every field of both configurations is equal.
     */
    bool operator==(const RendBufferDescVK& p_other) const;
};

/**
 * @class RendImageDescVK
 * @brief Configuration of a device-local image allocated by the render graph during graph compilation.
 */
struct RendImageDescVK {
    /** Primary usage; must be one of the IMAGE_* usages. */
    RendResPrimaryUsageVK usage = RendResPrimaryUsageVK::IMAGE_READ_WRITE;
    /** Vulkan usage flags added to the flags selected by the primary usage. */
    VkImageUsageFlags additional_usage = 0;
    /** Image format. */
    VkFormat format = VK_FORMAT_UNDEFINED;
    /** Image extent. A two-dimensional image uses a depth of one. */
    VkExtent3D extent{.width = 0, .height = 0, .depth = 1};
    /** Image dimensionality. */
    VkImageType image_type = VK_IMAGE_TYPE_2D;
    /** Number of mip levels. */
    uint32_t mip_levels = 1;
    /** Number of array layers. */
    uint32_t layers = 1;
    /** Whether one image is allocated per frame in flight instead of one for the whole graph. */
    bool per_frame = false;

    /**
     * @brief Compare two configurations.
     * @param p_other The configuration to compare with.
     * @return True if every field of both configurations is equal.
     */
    bool operator==(const RendImageDescVK& p_other) const;
};

/**
 * @class RendImageViewDescVK
 * @brief Configuration of an image view created by the render graph over a graph image resource.
 *
 * Unset optional fields are derived from the viewed image.
 */
struct RendImageViewDescVK {
    /** View type. Defaults to the image's default view type. */
    std::optional<VkImageViewType> view_type;
    /** View format. Defaults to the image format. */
    std::optional<VkFormat> format;
    /** Aspect mask. Defaults to the source image's color, depth, and/or stencil aspects. */
    std::optional<VkImageAspectFlags> aspect_mask;
    /** First viewed mip level. */
    uint32_t base_mip_level = 0;
    /** Number of viewed mip levels; VK_REMAINING_MIP_LEVELS views every level from base_mip_level. */
    uint32_t mip_level_count = 1;
    /** First viewed array layer. */
    uint32_t base_array_layer = 0;
    /** Number of viewed array layers; VK_REMAINING_ARRAY_LAYERS views every layer from base_array_layer. */
    uint32_t layer_count = 1;

    /**
     * @brief Compare two configurations.
     * @param p_other The configuration to compare with.
     * @return True if every field of both configurations is equal.
     */
    bool operator==(const RendImageViewDescVK& p_other) const;
};

/**
 * @class RendSamplerDescVK
 * @brief Configuration of a sampler created by the render graph during graph compilation.
 */
struct RendSamplerDescVK {
    /** Minification filter. */
    VkFilter min_filter = VK_FILTER_LINEAR;
    /** Magnification filter. */
    VkFilter mag_filter = VK_FILTER_LINEAR;
    /** Mipmap sampling mode. */
    VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    /** Address mode for the U coordinate. */
    VkSamplerAddressMode address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    /** Address mode for the V coordinate. */
    VkSamplerAddressMode address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    /** Address mode for the W coordinate. */
    VkSamplerAddressMode address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    /** Mipmap level-of-detail bias. */
    float mip_lod_bias = 0.0F;
    /** Maximum anisotropy; anisotropic filtering is disabled when unset. */
    std::optional<float> max_anisotropy;
    /** Depth-comparison operation; comparison sampling is disabled when unset. */
    std::optional<VkCompareOp> compare_op;
    /** Minimum mip level of detail. */
    float min_lod = 0.0F;
    /** Maximum mip level of detail. */
    float max_lod = VK_LOD_CLAMP_NONE;
    /** Border color used by clamp-to-border address modes. */
    VkBorderColor border_color = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    /** Whether texture coordinates are unnormalized. */
    bool unnormalized_coordinates = false;

    /**
     * @brief Compare two configurations.
     * @param p_other The configuration to compare with.
     * @return True if every field of both configurations is equal.
     */
    bool operator==(const RendSamplerDescVK& p_other) const;
};

/**
 * @brief Factory of the configuration of a graph-owned buffer, evaluated against the render graph context.
 */
using RendBufferFactoryVK = std::function<RendBufferDescVK(const RendGraphContextVK&)>;

/**
 * @brief Factory of the configuration of a graph-owned image, evaluated against the render graph context.
 */
using RendImageFactoryVK = std::function<RendImageDescVK(const RendGraphContextVK&)>;

/**
 * @brief Factory of the configuration of a graph-owned image view, evaluated against the render graph context.
 */
using RendImageViewFactoryVK = std::function<RendImageViewDescVK(const RendGraphContextVK&)>;

/**
 * @brief Factory of the configuration of a graph-owned sampler, evaluated against the render graph context.
 */
using RendSamplerFactoryVK = std::function<RendSamplerDescVK(const RendGraphContextVK&)>;

/**
 * @class RendBufferDeclVK
 * @brief A buffer resource declared on a graph descriptor.
 */
struct RendBufferDeclVK {
    /** Graph resource identifier. */
    std::string res_id;
    /** Factory of the buffer configuration. */
    RendBufferFactoryVK factory;
};

/**
 * @class RendImageDeclVK
 * @brief An image resource declared on a graph descriptor.
 */
struct RendImageDeclVK {
    /** Graph resource identifier. */
    std::string res_id;
    /** Factory of the image configuration. */
    RendImageFactoryVK factory;
};

/**
 * @class RendImageViewDeclVK
 * @brief An image view resource declared on a graph descriptor.
 */
struct RendImageViewDeclVK {
    /** Graph resource identifier of the view. */
    std::string res_id;
    /** Graph resource identifier of the viewed image. */
    std::string image_id;
    /** Factory of the view configuration. */
    RendImageViewFactoryVK factory;
};

/**
 * @class RendSamplerDeclVK
 * @brief A sampler resource declared on a graph descriptor.
 */
struct RendSamplerDeclVK {
    /** Graph resource identifier. */
    std::string res_id;
    /** Factory of the sampler configuration. */
    RendSamplerFactoryVK factory;
};

/**
 * @class RendResEdgeDescVK
 * @brief A resource dependency declared from one task descriptor to a later task.
 */
struct RendResEdgeDescVK {
    /** Identifier of the task that uses the resource next. */
    RendTaskID next_task_id;
    /** Graph resource identifier carried by the edge. */
    std::string res_id;
};

/**
 * @brief Factory of a render task, evaluated against the render graph context.
 *
 * The graph calls it when it is built and again whenever the context changes, and owns every task it returns; the
 * previous tasks are destroyed before their replacements are created. State that must survive a rebuild belongs outside
 * the task, and code that keeps a handle to the task must take it again from within the factory. The factory must return
 * a non-null task, and is called on the rendering thread.
 */
using RendTaskFactoryVK = std::function<Unique<IRenderTaskVK>(const RendGraphContextVK&)>;

/**
 * @class RendTaskDescVK
 * @brief A high-level Vulkan render task descriptor: the task factory, its resource dependencies, and its resource map.
 *
 * Copyable and movable; copies share the factory's captured state.
 */
struct RendTaskDescVK {
    /** @brief Construct an empty task descriptor backed by the supplied allocator. */
    explicit RendTaskDescVK(RenderAllocator* p_allocator);
    /**
     * @brief Construct a descriptor from a task factory.
     *
     * @param p_allocator Allocator used by the dependency list and resource map.
     * @param p_task_id Task id used for graph dependencies.
     * @param p_factory Factory of the task.
     * @throws std::runtime_error If p_factory is null.
     */
    RendTaskDescVK(RenderAllocator* p_allocator, std::string_view p_task_id, RendTaskFactoryVK p_factory);
    ~RendTaskDescVK() = default;
    RendTaskDescVK(const RendTaskDescVK&) = default;
    RendTaskDescVK(RendTaskDescVK&&) noexcept = default;
    RendTaskDescVK& operator=(const RendTaskDescVK&) = default;
    RendTaskDescVK& operator=(RendTaskDescVK&&) noexcept = default;

    /**
     * @brief Connect this task to a later task through a resource dependency.
     *
     * The edge orders this task before the next task and synchronizes the resource between them: with a pipeline barrier
     * when both tasks run on the same queue, otherwise with a timeline semaphore. Aggregate resource-list ids apply to every
     * indexed entry. The graph derives the synchronization stages and accesses from the usages both tasks report for the
     * resource through IRenderTaskVK::append_accesses(); graph construction fails if either task does not access it.
     *
     * @param p_next_task_id Identifier of the task that uses the resource next.
     * @param p_resource_id Graph resource identifier carried by the edge.
     * @return This descriptor.
     */
    RendTaskDescVK& connect(std::string_view p_next_task_id, std::string_view p_resource_id);

    /**
     * @brief Map a resource identifier used by the task to a graph resource.
     *
     * The task binds and reports its accesses with task-local identifiers: reflected shader resource ids, render targets
     * (`render_target_colors[<index>]` and `render_target_depth`), and ids passed to its constructor. The graph resolves each
     * of them through this map; unmapped identifiers must name graph resources directly. Every alias must be used by the task.
     *
     * @param p_task_resource_id Identifier used by the task.
     * @param p_graph_resource_id Graph resource identifier it refers to.
     * @return This descriptor.
     * @throws std::runtime_error If an identifier is empty or p_task_resource_id is already mapped.
     */
    RendTaskDescVK& alias(std::string_view p_task_resource_id, std::string_view p_graph_resource_id);

    // The id of this task within the graph.
    RendTaskID task_id;
    // Resource dependencies from this task to later tasks.
    Vector<RendResEdgeDescVK, RenderAllocator> next;
    // Task-local to graph resource identifier map.
    RenderTaskResourceMap resource_map;
    // Factory of the concrete task, called by the graph when it is built and whenever the graph context changes.
    RendTaskFactoryVK factory;
};

/**
 * @class RendSubgraphVK
 * @brief A subgraph added to a graph descriptor: a named group of flattened tasks that other tasks connect to as one.
 *
 * An edge to the subgraph on a resource connects to the members that access the resource and have no edge on it from
 * another member; an edge from the subgraph leaves from the members that access it and have no edge on it to another member.
 * The graph resolves both when it is built, from the accesses the tasks report. Copyable and movable.
 */
struct RendSubgraphVK {
    /**
     * @brief Construct an empty subgraph record.
     * @param p_allocator Allocator used by the member and dependency lists.
     * @param p_subgraph_id Identifier of the subgraph within the graph.
     */
    RendSubgraphVK(RenderAllocator* p_allocator, std::string_view p_subgraph_id);
    ~RendSubgraphVK() = default;
    RendSubgraphVK(const RendSubgraphVK&) = default;
    RendSubgraphVK(RendSubgraphVK&&) noexcept = default;
    RendSubgraphVK& operator=(const RendSubgraphVK&) = default;
    RendSubgraphVK& operator=(RendSubgraphVK&&) noexcept = default;

    /**
     * @brief Connect the subgraph to a later task or subgraph through a resource dependency; see RendTaskDescVK::connect().
     * @param p_next_task_id Identifier of the task or subgraph that uses the resource next.
     * @param p_resource_id Graph resource identifier carried by the edge.
     * @return This record.
     */
    RendSubgraphVK& connect(std::string_view p_next_task_id, std::string_view p_resource_id);

    // The id other tasks connect to; also the prefix of its members' ids.
    RendTaskID subgraph_id;
    // The ids of the tasks inside the subgraph, those of nested subgraphs included.
    Vector<RendTaskID, RenderAllocator> members;
    // Resource dependencies from the subgraph to later tasks or subgraphs.
    Vector<RendResEdgeDescVK, RenderAllocator> next;
};

/**
 * @class RendSubgraphPortBindingVK
 * @brief Binds a port of a subgraph to the resource of the enclosing graph it stands for.
 */
struct RendSubgraphPortBindingVK {
    /** The port, a resource identifier inside the subgraph. */
    std::string_view port;
    /** The graph resource identifier it is bound to. */
    std::string_view resource;
};

struct RendSubgraphDescVK;

/**
 * @class RendGraphDescVK
 * @brief A high-level Vulkan render pipeline descriptor.
 *
 * Every resource is declared once in this graph-wide descriptor. Buffers, images, image views, and samplers declared with
 * add_buffer(), add_image(), add_image_view(), and add_sampler() are allocated and owned by the graph when it is compiled.
 * They are declared through factories of their configurations, and tasks through task factories; whenever the
 * RendGraphContextVK changes, such as when the swap chain is resized, the graph re-evaluates every resource factory and
 * reallocates the resources whose configuration changed, together with the views of reallocated images, then recreates
 * every task and rebuilds its dependency edges and synchronization. Reallocated resources lose their content and are
 * replaced in place in their containers, so tasks and RenderGraphVK::acquire_resource() callers observe the new objects; resources
 * owned elsewhere are imported with add_resource(), which binds any IBindableRenderResourceVK. Tasks refer to every resource
 * by id through their reflected shader, attachment, and fixed-function accesses, resolved through each task's resource map.
 */
struct RendGraphDescVK {
    explicit RendGraphDescVK(RenderAllocator* p_allocator)
        : allocator(p_allocator), tasks(p_allocator), resources(p_allocator), buffers(p_allocator), images(p_allocator),
          image_views(p_allocator), samplers(p_allocator), subgraphs(p_allocator) {
    }

    /**
     * @brief Append a task descriptor to the graph.
     * @param p_task_desc The task descriptor to transfer into the graph.
     * @return This graph descriptor.
     */
    RendGraphDescVK& add_task(RendTaskDescVK p_task_desc);

    /**
     * @brief Append a task to the graph from its factory.
     * @param p_task_id The task id used for graph dependencies.
     * @param p_factory Factory of the task.
     * @return This graph descriptor.
     * @throws std::runtime_error If p_factory is null.
     */
    RendGraphDescVK& add_task(std::string_view p_task_id, RendTaskFactoryVK p_factory);

    /**
     * @brief Get a task descriptor that was already added, typically to connect it to later tasks.
     *
     * The reference is invalidated when another task is added.
     *
     * @param p_task_id The task id.
     * @return The stored task descriptor.
     * @throws std::runtime_error If no task has the id.
     */
    RendTaskDescVK& get_task(std::string_view p_task_id);

    /**
     * @brief Append a resource binding to the graph.
     * @param p_resource The resource binding to transfer into the graph.
     * @return This graph descriptor.
     */
    RendGraphDescVK& add_resource(RendResVK p_resource);

    /**
     * @brief Import a resource owned outside the graph under p_resource_id.
     *
     * Calls p_resource.bind(), which retains strong references to the imported render objects. A composite registers its
     * members under `<p_resource_id>.<member>`.
     *
     * @param p_resource_id Graph resource identifier, or the key prefix of a composite.
     * @param p_resource Resource to import; it need not outlive this call.
     * @return This graph descriptor.
     * @throws std::runtime_error If the resource cannot be bound.
     */
    RendGraphDescVK& add_resource(std::string_view p_resource_id, const IBindableRenderResourceVK& p_resource);

    /**
     * @brief Declare a buffer allocated by the graph during compilation.
     *
     * The factory is evaluated when the graph is compiled and again whenever the graph context changes; it must keep
     * per_frame unchanged. The buffer is reallocated, and its initial data uploaded again, when the configuration changes.
     *
     * @param p_resource_id Graph resource identifier.
     * @param p_factory Factory of the buffer configuration.
     * @return This graph descriptor.
     * @throws std::runtime_error If p_resource_id is empty or p_factory is null.
     */
    RendGraphDescVK& add_buffer(std::string_view p_resource_id, RendBufferFactoryVK p_factory);

    /**
     * @brief Declare an image allocated by the graph during compilation.
     *
     * The factory is evaluated when the graph is compiled and again whenever the graph context changes; it must keep
     * per_frame unchanged. The image is reallocated when the configuration changes.
     *
     * @param p_resource_id Graph resource identifier.
     * @param p_factory Factory of the image configuration.
     * @return This graph descriptor.
     * @throws std::runtime_error If p_resource_id is empty or p_factory is null.
     */
    RendGraphDescVK& add_image(std::string_view p_resource_id, RendImageFactoryVK p_factory);

    /**
     * @brief Declare an image view created by the graph during compilation.
     *
     * A view of a per-frame image is itself per-frame. The factory is evaluated when the graph is compiled and again
     * whenever the graph context changes. The view is recreated when its configuration changes or the viewed image is
     * reallocated.
     *
     * @param p_resource_id Graph resource identifier of the view.
     * @param p_image_id Graph resource identifier of the viewed image, allocated by the graph or imported as an ROImageVK.
     * @param p_factory Factory of the view configuration; null selects the default RendImageViewDescVK.
     * @return This graph descriptor.
     * @throws std::runtime_error If p_resource_id or p_image_id is empty.
     */
    RendGraphDescVK& add_image_view(std::string_view p_resource_id, std::string_view p_image_id, RendImageViewFactoryVK p_factory = nullptr);

    /**
     * @brief Declare a sampler created by the graph during compilation.
     *
     * The factory is evaluated when the graph is compiled and again whenever the graph context changes. The sampler is
     * recreated when its configuration changes.
     *
     * @param p_resource_id Graph resource identifier.
     * @param p_factory Factory of the sampler configuration; null selects the default RendSamplerDescVK.
     * @return This graph descriptor.
     * @throws std::runtime_error If p_resource_id is empty.
     */
    RendGraphDescVK& add_sampler(std::string_view p_resource_id, RendSamplerFactoryVK p_factory = nullptr);
    /**
     * @brief Add a subgraph, flattened into this descriptor under p_subgraph_id.
     *
     * Every task and nested subgraph of the subgraph gets the id `<p_subgraph_id>.<id>`, and every resource it declares with
     * add_buffer(), add_image(), add_image_view(), or add_sampler() the id `<p_subgraph_id>.<id>`. Resource identifiers used
     * inside it, by its tasks' accesses and aliases, its edges, and its views' images, name a port, which resolves to the
     * resource it is bound to; else a resource it declares; else a resource of the enclosing graph, unchanged. Its imports are
     * imports of this descriptor; one the descriptor already imports under the same id must be the same resource. Edges
     * inside the subgraph must stay inside it. Members keep their factories; other tasks may connect to them directly.
     *
     * @param p_subgraph_id Identifier other tasks connect to; it must not be the id of a task or subgraph of this descriptor.
     * @param p_subgraph The subgraph; it is copied.
     * @param p_ports Binding of every port of p_subgraph.
     * @return The added subgraph, to connect it to later tasks; invalidated when another subgraph is added.
     * @throws std::runtime_error If p_subgraph_id is empty or taken, a port is unbound, bound twice, unknown, or names a
     * resource the subgraph declares or imports, an edge inside it leaves it, or an import conflicts with this descriptor's.
     */
    RendSubgraphVK& add_subgraph(
        std::string_view p_subgraph_id, const RendSubgraphDescVK& p_subgraph, std::span<const RendSubgraphPortBindingVK> p_ports);

    /**
     * @brief Get a subgraph that was already added, typically to connect it to later tasks.
     *
     * The reference is invalidated when another subgraph is added.
     *
     * @param p_subgraph_id The subgraph id.
     * @return The stored subgraph record.
     * @throws std::runtime_error If no subgraph has the id.
     */
    RendSubgraphVK& get_subgraph(std::string_view p_subgraph_id);

    /**
     * @brief Check whether a resource is already imported or declared under an id.
     * @param p_resource_id Graph resource identifier; a composite's members are checked by their full `<key>.<member>` ids.
     */
    bool contains_resource(std::string_view p_resource_id) const;

    RenderAllocator* allocator = nullptr;
    // The set of tasks in the pipeline. Order is irrelevant; the graph topo-
    // sorts them based on their resource dependency edges.
    Vector<RendTaskDescVK, RenderAllocator> tasks;
    // Imported resources used by the pipeline. Looked up by res_id.
    Vector<RendResVK, RenderAllocator> resources;
    // Resources allocated and owned by the graph during compilation.
    Vector<RendBufferDeclVK, RenderAllocator> buffers;
    Vector<RendImageDeclVK, RenderAllocator> images;
    Vector<RendImageViewDeclVK, RenderAllocator> image_views;
    Vector<RendSamplerDeclVK, RenderAllocator> samplers;
    // Subgraphs added with add_subgraph(); their members are in tasks. The graph resolves the edges to and from them.
    Vector<RendSubgraphVK, RenderAllocator> subgraphs;
};

/**
 * @class RendSubgraphDescVK
 * @brief A subgraph to add to a graph descriptor with RendGraphDescVK::add_subgraph(): a descriptor of its own, whose
 * tasks, declared resources, imports, and nested subgraphs it holds, and the ports it is connected through.
 *
 * A port is a resource identifier the subgraph's tasks use for a resource of the enclosing graph, bound when the subgraph is
 * added. Copyable and movable; copies share the task factories' captured state.
 */
struct RendSubgraphDescVK {
    /** @brief Construct an empty subgraph backed by the supplied allocator. */
    explicit RendSubgraphDescVK(RenderAllocator* p_allocator) : graph(p_allocator), ports(p_allocator) {
    }

    /**
     * @brief Declare a port.
     * @param p_port Resource identifier the subgraph uses for the bound resource.
     * @return This subgraph.
     * @throws std::runtime_error If p_port is empty or already declared.
     */
    RendSubgraphDescVK& add_port(std::string_view p_port);

    // The tasks, resources, and nested subgraphs of the subgraph.
    RendGraphDescVK graph;
    // The ports, each bound to a resource of the enclosing graph when the subgraph is added.
    Vector<std::string, RenderAllocator> ports;
};

} // namespace WhiteBirdEngine

#endif

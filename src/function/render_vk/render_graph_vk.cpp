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
#include "function/render_vk/render_graph_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/i_render_task.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/rend_graph_resource_diff_vk.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_metadata_vk.hh"
#include "function/render_vk/render_plugin_vk.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/ro_sampler_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/roc_single_vk.hh"
#include "function/render_vk/semaphore_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

void register_resource_list(Map<std::string, Ref<RendResVK>, RenderAllocator>* p_resources,
    RenderAllocator* p_allocator,
    std::string_view p_resource_id,
    Ref<RenderResourceListVK> p_resource_list) {
    if (p_resources == nullptr || p_allocator == nullptr || p_resource_id.empty() || p_resource_list == MEM_NULL) {
        throw std::runtime_error("RenderGraphVK: invalid render resource list registration.");
    }
    if (p_resources->contains(p_resource_id)) {
        throw std::runtime_error(std::format("RenderGraphVK: duplicate resource id: {}.", p_resource_id));
    }
    Ref<RendResVK> aggregate = make_ref<RendResVK>(p_allocator);
    aggregate->res_id = p_resource_id;
    aggregate->res_type_id = static_hash("RenderResourceListVK");
    aggregate->resource_list = p_resource_list;
    p_resources->emplace(std::string(p_resource_id), std::move(aggregate));

    for (size_t index = 0; index < p_resource_list->get_resource_count(); ++index) {
        const std::string indexed_id = std::format("{}[{}]", p_resource_id, index);
        if (p_resources->contains(indexed_id)) {
            throw std::runtime_error(std::format("RenderGraphVK: duplicate resource-list entry id: {}.", indexed_id));
        }
        Ref<IRenderObjectVK> resource = p_resource_list->acquire_resource(index).dynamic_cast_ref<IRenderObjectVK>();
        if (resource == MEM_NULL) {
            throw std::runtime_error(std::format("RenderGraphVK: resource-list entry '{}' is not a Vulkan render object.", indexed_id));
        }
        Ref<ROCSingleVK> container = make_ref<ROCSingleVK>(p_allocator, [resource](uint32_t /*p_slot*/) {
            return resource;
        });
        Ref<RendResVK> indexed_resource = make_ref<RendResVK>(p_allocator);
        indexed_resource->res_id = indexed_id;
        indexed_resource->res_type_id = container->get_resource_type_id();
        indexed_resource->container = std::move(container);
        p_resources->emplace(indexed_id, std::move(indexed_resource));
    }
}

Ref<IRenderObjectVK> resolve_render_object(const RendResVK& p_resource, uint32_t p_slot) {
    if (p_resource.container == MEM_NULL) {
        return MEM_NULL;
    }
    return p_resource.container->get(p_slot).dynamic_cast_ref<IRenderObjectVK>();
}

DebugData make_descriptor_data(const DebugDataRows& p_rows) {
    DebugDataArray rows;
    rows.reserve(p_rows.size());
    for (const DebugData& row : p_rows) {
        rows.push_back(row);
    }
    DebugData root;
    root.set(std::move(rows));
    return root;
}

uint32_t descriptor_heap_index(const DebugData& p_row) {
    return p_row.get_value<uint32_t>("heap_index");
}

std::string descriptor_heap(const DebugData& p_row) {
    return p_row.get_value<std::string>("heap");
}

std::string descriptor_resource_id(const DebugData& p_row) {
    return p_row.get_value<std::string>("resource_id");
}

int64_t descriptor_slot_index(const DebugData& p_row) {
    if (!p_row.contains("slot_index")) {
        return -1;
    }
    return static_cast<int64_t>(p_row.get_value<uint32_t>("slot_index"));
}

std::string descriptor_type_name(const DebugData& p_row) {
    return p_row.get_value<std::string>("descriptor_type");
}

VkQueue queue_for(const RenderContextVK* p_render_data, QueueFamilyVK p_queue_family) {
    switch (p_queue_family) {
    case QueueFamilyVK::GRAPHICS:
        return p_render_data->graphics_queue;
    case QueueFamilyVK::COMPUTE_1:
        return p_render_data->compute_queue_1;
    case QueueFamilyVK::COMPUTE_2:
        return p_render_data->compute_queue_2;
    }
    return VK_NULL_HANDLE;
}

VkCommandPool pool_for(const RenderContextVK* p_render_data, QueueFamilyVK p_queue_family) {
    switch (p_queue_family) {
    case QueueFamilyVK::GRAPHICS:
        return p_render_data->graphics_command_pool;
    case QueueFamilyVK::COMPUTE_1:
    case QueueFamilyVK::COMPUTE_2:
        return p_render_data->compute_command_pool;
    }
    return VK_NULL_HANDLE;
}

struct ResourceScopeVK {
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
};

QueueFamilyVK node_queue_family(Ref<RGNode> p_node) {
    return p_node->in_queue;
}

VkPipelineStageFlags2 shader_stage_scope(QueueFamilyVK p_queue_family) {
    switch (p_queue_family) {
    case QueueFamilyVK::GRAPHICS:
        return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case QueueFamilyVK::COMPUTE_1:
    case QueueFamilyVK::COMPUTE_2:
        return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    }
    throw std::runtime_error("RenderGraphVK: unknown queue family.");
}

void ensure_graphics_queue_usage(QueueFamilyVK p_queue_family, RendResUsageVK p_usage) {
    if (p_queue_family != QueueFamilyVK::GRAPHICS) {
        throw std::runtime_error(
            std::format("RenderGraphVK: resource usage {} is not valid on the compute queue.", std::to_underlying(p_usage)));
    }
}

ResourceScopeVK resource_scope(RendResUsageVK p_usage, bool p_write, QueueFamilyVK p_queue_family) {
    switch (p_usage) {
    case RendResUsageVK::SAMPLED_IMAGE:
        if (p_write) {
            break;
        }
        return {.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, .stage = shader_stage_scope(p_queue_family)};
    case RendResUsageVK::COLOR_ATTACHMENT:
        ensure_graphics_queue_usage(p_queue_family, p_usage);
        return {
            .access = p_write ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    case RendResUsageVK::DEPTH_ATTACHMENT:
        ensure_graphics_queue_usage(p_queue_family, p_usage);
        return {
            .access = p_write ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        };
    case RendResUsageVK::STORAGE_IMAGE:
        return {
            .access = p_write ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .stage = shader_stage_scope(p_queue_family),
        };
    case RendResUsageVK::UNIFORM_BUFFER:
        if (p_write) {
            break;
        }
        return {.access = VK_ACCESS_2_UNIFORM_READ_BIT, .stage = shader_stage_scope(p_queue_family)};
    case RendResUsageVK::STORAGE_BUFFER:
        return {
            .access = p_write ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .stage = shader_stage_scope(p_queue_family),
        };
    case RendResUsageVK::ACCELERATION_STRUCTURE:
        return {
            .access = p_write ? VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR : VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            .stage = p_write ? VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR : shader_stage_scope(p_queue_family),
        };
    case RendResUsageVK::TRANSFER_SRC:
        if (p_write) {
            break;
        }
        return {.access = VK_ACCESS_2_TRANSFER_READ_BIT, .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT};
    case RendResUsageVK::TRANSFER_DST:
        if (!p_write) {
            break;
        }
        return {.access = VK_ACCESS_2_TRANSFER_WRITE_BIT, .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT};
    case RendResUsageVK::INDIRECT_BUFFER:
        if (p_write) {
            break;
        }
        // Indirect draws and indirect dispatches both read their commands in the draw-indirect stage, which compute queues
        // also support.
        return {.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT};
    case RendResUsageVK::VERTEX_BUFFER:
        if (p_write) {
            break;
        }
        ensure_graphics_queue_usage(p_queue_family, p_usage);
        return {.access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, .stage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT};
    case RendResUsageVK::INDEX_BUFFER:
        if (p_write) {
            break;
        }
        ensure_graphics_queue_usage(p_queue_family, p_usage);
        return {.access = VK_ACCESS_2_INDEX_READ_BIT, .stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT};
    case RendResUsageVK::ALL_COMMANDS:
        return {.access = p_write ? VK_ACCESS_2_MEMORY_WRITE_BIT : VK_ACCESS_2_MEMORY_READ_BIT, .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    }
    throw std::runtime_error("RenderGraphVK: resource usage is invalid for the declared read/write direction.");
}

void validate_declared_resource(
    const Map<std::string, Ref<RendResVK>, RenderAllocator>& p_resources, const RGNode& p_node, const ResAccessVK& p_access, bool p_is_write) {
    if (!p_resources.contains(p_access.res_id)) {
        throw std::runtime_error(std::format("RenderGraphVK: task '{}' {} undeclared resource '{}'.",
            p_node.task_id,
            p_is_write ? "writes" : "reads",
            p_access.res_id));
    }
}

struct EdgeScopeVK {
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 read_access = VK_ACCESS_2_NONE;
    VkAccessFlags2 write_access = VK_ACCESS_2_NONE;
};

// Merge the synchronization scopes of every usage the node reports for the resource.
EdgeScopeVK node_resource_scope(const RGNode& p_node, std::string_view p_resource_id) {
    EdgeScopeVK result;
    auto merge = [&](const Vector<ResAccessVK, RenderAllocator>& p_accesses, bool p_write) {
        for (const ResAccessVK& access : p_accesses) {
            if (access.res_id != p_resource_id) {
                continue;
            }
            ResourceScopeVK scope = resource_scope(access.usage, p_write, p_node.in_queue);
            result.stage |= scope.stage;
            (p_write ? result.write_access : result.read_access) |= scope.access;
        }
    };
    merge(p_node.reads, false);
    merge(p_node.writes, true);
    return result;
}

VkBufferUsageFlags buffer_usage_flags(RendResPrimaryUsageVK p_usage, std::string_view p_resource_id) {
    switch (p_usage) {
    case RendResPrimaryUsageVK::BUFFER_UNIFORM:
        return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case RendResPrimaryUsageVK::BUFFER_STORAGE:
        return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case RendResPrimaryUsageVK::BUFFER_INDIRECT:
        return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case RendResPrimaryUsageVK::BUFFER_VERTEX:
        return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case RendResPrimaryUsageVK::BUFFER_INDEX:
        return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    default:
        break;
    }
    throw std::runtime_error(std::format("RenderGraphVK: buffer '{}' has a non-buffer primary usage.", p_resource_id));
}

VkImageUsageFlags image_usage_flags(RendResPrimaryUsageVK p_usage, std::string_view p_resource_id) {
    switch (p_usage) {
    case RendResPrimaryUsageVK::IMAGE_READ_ONLY:
        return VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    case RendResPrimaryUsageVK::IMAGE_READ_WRITE:
        return VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    case RendResPrimaryUsageVK::IMAGE_RENDER_TARGET:
        return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    case RendResPrimaryUsageVK::IMAGE_DEPTH_TARGET:
        return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    default:
        break;
    }
    throw std::runtime_error(std::format("RenderGraphVK: image '{}' has a non-image primary usage.", p_resource_id));
}

ROSamplerVK::Builder make_sampler_builder(const RendSamplerDescVK& p_desc) {
    ROSamplerVK::Builder builder;
    builder.set_filter(p_desc.min_filter, p_desc.mag_filter)
        .set_mipmap_mode(p_desc.mipmap_mode)
        .set_address_mode(p_desc.address_mode_u, p_desc.address_mode_v, p_desc.address_mode_w)
        .set_mip_lod_bias(p_desc.mip_lod_bias)
        .set_anisotropy(p_desc.max_anisotropy.has_value() ? VK_TRUE : VK_FALSE, p_desc.max_anisotropy.value_or(1.0F))
        .set_compare(p_desc.compare_op.has_value() ? VK_TRUE : VK_FALSE, p_desc.compare_op.value_or(VK_COMPARE_OP_ALWAYS))
        .set_lod_range(p_desc.min_lod, p_desc.max_lod)
        .set_border_color(p_desc.border_color)
        .set_unnormalized_coordinates(p_desc.unnormalized_coordinates ? VK_TRUE : VK_FALSE);
    return builder;
}

Ref<ROImageViewVK> make_image_view(Ref<ROImageVK> p_image,
    std::string_view p_view_id,
    std::string_view p_image_id,
    const RendImageViewDescVK& p_desc) {
    const RendImageViewDescVK& desc = p_desc;
    const ROImageVK::ExposedFields& image = p_image->fields();
    if (desc.base_mip_level >= image.mipmap_levels || desc.base_array_layer >= image.layers_count) {
        throw std::runtime_error(
            std::format("RenderGraphVK: image view '{}' starts outside image '{}'.", p_view_id, p_image_id));
    }
    const uint32_t mip_level_count =
        desc.mip_level_count == VK_REMAINING_MIP_LEVELS ? image.mipmap_levels - desc.base_mip_level : desc.mip_level_count;
    const uint32_t layer_count =
        desc.layer_count == VK_REMAINING_ARRAY_LAYERS ? image.layers_count - desc.base_array_layer : desc.layer_count;
    if (mip_level_count == 0 || layer_count == 0 || mip_level_count > image.mipmap_levels - desc.base_mip_level ||
        layer_count > image.layers_count - desc.base_array_layer) {
        throw std::runtime_error(
            std::format("RenderGraphVK: image view '{}' exceeds image '{}'.", p_view_id, p_image_id));
    }
    return p_image->get_image_view_ref(desc.view_type.value_or(image.view_type),
        desc.format.value_or(image.format),
        VkImageSubresourceRange{.aspectMask = desc.aspect_mask.value_or(0),
            .baseMipLevel = desc.base_mip_level,
            .levelCount = mip_level_count,
            .baseArrayLayer = desc.base_array_layer,
            .layerCount = layer_count},
        VkComponentMapping{});
}

// Configuration errors are reported before any container is constructed or recreated, so that a failed graph build
// leaves no partially constructed container behind.
void validate_buffer_desc(std::string_view p_resource_id, const RendBufferDescVK& p_desc) {
    const size_t size = p_desc.size == 0 ? p_desc.initial_data.size() : p_desc.size;
    if (size == 0) {
        throw std::runtime_error(std::format("RenderGraphVK: buffer '{}' has no size.", p_resource_id));
    }
    if (p_desc.initial_data.size() > size) {
        throw std::runtime_error(std::format("RenderGraphVK: initial data of buffer '{}' exceeds its size.", p_resource_id));
    }
    static_cast<void>(buffer_usage_flags(p_desc.usage, p_resource_id));
}

void validate_image_desc(std::string_view p_resource_id, const RendImageDescVK& p_desc) {
    if (p_desc.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error(std::format("RenderGraphVK: image '{}' has an undefined format.", p_resource_id));
    }
    if (p_desc.extent.width == 0 || p_desc.extent.height == 0 || p_desc.extent.depth == 0) {
        throw std::runtime_error(std::format("RenderGraphVK: image '{}' has an empty extent {}x{}x{}.",
            p_resource_id,
            p_desc.extent.width,
            p_desc.extent.height,
            p_desc.extent.depth));
    }
    if (p_desc.mip_levels == 0) {
        throw std::runtime_error(std::format("RenderGraphVK: image '{}' has no mip levels.", p_resource_id));
    }
    if (p_desc.layers == 0) {
        throw std::runtime_error(std::format("RenderGraphVK: image '{}' has no array layers.", p_resource_id));
    }
    static_cast<void>(image_usage_flags(p_desc.usage, p_resource_id));
}

Ref<IRenderObjectVK> make_buffer(const RenderContextVK* p_render_data, std::string_view p_resource_id, const RendBufferDescVK& p_desc) {
    WBE_DEBUG_ASSERT(p_desc.size != 0 || !p_desc.initial_data.empty());
    const size_t size = p_desc.size == 0 ? p_desc.initial_data.size() : p_desc.size;
    ROBufferVK::Builder builder;
    if (!p_desc.initial_data.empty()) {
        builder.initial_data(p_desc.initial_data, p_desc.domain);
    }
    builder.set_size(size).set_domain(p_desc.domain).add_usage(buffer_usage_flags(p_desc.usage, p_resource_id) | p_desc.additional_usage);
    return p_render_data->create_buffer(builder);
}

Ref<IRenderObjectVK> make_image(const RenderContextVK* p_render_data, std::string_view p_resource_id, const RendImageDescVK& p_desc) {
    WBE_DEBUG_ASSERT(p_desc.format != VK_FORMAT_UNDEFINED);
    ROImageVK::Builder builder;
    builder.set_image_type(p_desc.image_type)
        .set_format(p_desc.format)
        .set_extent(p_desc.extent)
        .set_mipmap_level(p_desc.mip_levels)
        .set_layers(p_desc.layers)
        .set_usage(image_usage_flags(p_desc.usage, p_resource_id) | p_desc.additional_usage);
    return p_render_data->create_image(builder);
}

uint32_t container_slot_count(const RenderObjectContainerVK& p_container) {
    const auto* multislots = dynamic_cast<const ROCMultislotsVK*>(&p_container);
    return multislots != nullptr ? multislots->get_slot_count() : 1U;
}

// Whether a kept view no longer views the objects of its image container, because the image was recreated or an
// imported image was replaced outside the graph.
bool view_image_changed(const Ref<RenderObjectContainerVK>& p_view_container, const Ref<RenderObjectContainerVK>& p_image_container) {
    const uint32_t slot_count = container_slot_count(*p_view_container);
    for (uint32_t slot = 0; slot < slot_count; ++slot) {
        Ref<ROImageViewVK> view = p_view_container->get(slot).dynamic_cast_ref<ROImageViewVK>();
        WBE_DEBUG_ASSERT(view != MEM_NULL);
        if (view->fields().image != p_image_container->get(slot)) {
            return true;
        }
    }
    return false;
}

void validate_graph_resource_ids(const RendGraphDescVK& p_desc, const Map<std::string, Ref<RendResVK>, RenderAllocator>& p_imports) {
    // Reject duplicate ids before any GPU object is created.
    Set<std::string_view, RenderAllocator> declared_ids(p_desc.allocator);
    auto declare = [&p_imports, &declared_ids](std::string_view p_resource_id) {
        if (p_imports.contains(p_resource_id) || !declared_ids.insert(p_resource_id).second) {
            throw std::runtime_error(std::format("RenderGraphVK: duplicate resource id: {}.", p_resource_id));
        }
    };
    for (const RendBufferDeclVK& buffer_decl : p_desc.buffers) {
        declare(buffer_decl.res_id);
    }
    for (const RendImageDeclVK& image_decl : p_desc.images) {
        declare(image_decl.res_id);
    }
    for (const RendImageViewDeclVK& view_decl : p_desc.image_views) {
        declare(view_decl.res_id);
    }
    for (const RendSamplerDeclVK& sampler_decl : p_desc.samplers) {
        declare(sampler_decl.res_id);
    }
}

// Evaluate every resource of p_desc for p_context. Configuration errors are reported here, before any container is
// constructed or recreated, so that a failed build leaves the current graph untouched.
RendGraphResourceDiffVK::Signatures evaluate_signatures(const RendGraphDescVK& p_desc,
    const Map<std::string, Ref<RendResVK>, RenderAllocator>& p_imports,
    const RendGraphContextVK& p_context,
    RenderAllocator* p_allocator) {
    RendGraphResourceDiffVK::Signatures signatures(p_allocator);
    for (const RendResVK& import : p_desc.resources) {
        const void* identity = import.resource_list != MEM_NULL ? static_cast<const void*>(import.resource_list.get())
                                                                : static_cast<const void*>(import.container.get());
        signatures.emplace(import.res_id, RendResSignatureVK{.kind = RendResKindVK::IMPORT, .import_identity = identity});
    }
    for (const RendBufferDeclVK& buffer_decl : p_desc.buffers) {
        RendBufferDescVK desc = buffer_decl.factory(p_context);
        validate_buffer_desc(buffer_decl.res_id, desc);
        const bool per_frame = desc.per_frame;
        signatures.emplace(
            buffer_decl.res_id, RendResSignatureVK{.kind = RendResKindVK::BUFFER, .desc = std::move(desc), .per_frame = per_frame});
    }
    for (const RendImageDeclVK& image_decl : p_desc.images) {
        const RendImageDescVK desc = image_decl.factory(p_context);
        validate_image_desc(image_decl.res_id, desc);
        signatures.emplace(image_decl.res_id, RendResSignatureVK{.kind = RendResKindVK::IMAGE, .desc = desc, .per_frame = desc.per_frame});
    }
    for (const RendImageViewDeclVK& view_decl : p_desc.image_views) {
        // A view views a graph-owned image or an imported one, and has one object per object of the image.
        std::optional<bool> image_per_frame;
        const auto owned_image = signatures.find(view_decl.image_id);
        if (owned_image != signatures.end() && owned_image->second.kind == RendResKindVK::IMAGE) {
            image_per_frame = owned_image->second.per_frame;
        }
        const auto imported_image = p_imports.find(view_decl.image_id);
        if (!image_per_frame.has_value() && imported_image != p_imports.end() && imported_image->second->container != MEM_NULL &&
            imported_image->second->res_type_id == TypeUUID<ROImageVK>::UUID) {
            image_per_frame = imported_image->second->container.dynamic_cast_ref<ROCMultislotsVK>() != MEM_NULL;
        }
        if (!image_per_frame.has_value()) {
            throw std::runtime_error(std::format("RenderGraphVK: image view '{}' views '{}', which is not a declared image resource.",
                view_decl.res_id,
                view_decl.image_id));
        }
        signatures.emplace(view_decl.res_id,
            RendResSignatureVK{
                .kind = RendResKindVK::IMAGE_VIEW,
                .desc = view_decl.factory(p_context),
                .image_id = view_decl.image_id,
                .per_frame = *image_per_frame,
            });
    }
    for (const RendSamplerDeclVK& sampler_decl : p_desc.samplers) {
        signatures.emplace(sampler_decl.res_id, RendResSignatureVK{.kind = RendResKindVK::SAMPLER, .desc = sampler_decl.factory(p_context)});
    }
    return signatures;
}

} // namespace

struct RenderGraphVK::RebuildState {
    RebuildState() = delete;
    explicit RebuildState(RenderAllocator* p_allocator)
        : signatures(p_allocator), owned_containers(p_allocator), task_descs(p_allocator), subgraphs(p_allocator) {
    }
    ~RebuildState() = default;
    RebuildState(const RebuildState&) = delete;
    RebuildState(RebuildState&&) noexcept = delete;
    RebuildState& operator=(const RebuildState&) = delete;
    RebuildState& operator=(RebuildState&&) noexcept = delete;

    RendGraphRebuildCallbackVK callback;
    RendGraphResourceDiffVK::Signatures signatures;
    Map<std::string, Ref<RenderObjectContainerVK>, RenderAllocator> owned_containers;
    Vector<RendTaskDescVK, RenderAllocator> task_descs;
    Vector<RendSubgraphVK, RenderAllocator> subgraphs;
};

RenderGraphVK::RenderGraphVK(const RenderContextVK* p_render_data, std::string_view p_resource_key)
    : render_data(p_render_data), resource_key(p_resource_key), nodes(p_render_data->allocator), resource_edges(p_render_data->allocator),
      all_resources(p_render_data->allocator),
      rebuild_state(make_unique<RebuildState>(p_render_data->allocator, p_render_data->allocator)),
      command_buffers(p_render_data->allocator), command_buffer_pools(p_render_data->allocator),
      terminal_semaphores(p_render_data->allocator) {
}

RenderGraphVK::~RenderGraphVK() {
    free_command_buffers();
}

std::string_view RendResViewVK::resolve(std::string_view p_resource_id) const {
    return resource_map != nullptr ? resource_map->resolve(p_resource_id) : p_resource_id;
}

bool RendResViewVK::contains(std::string_view p_resource_id) const {
    auto resource = resources->find(resolve(p_resource_id));
    return resource != resources->end() && resource->second != MEM_NULL &&
           (resource->second->container != MEM_NULL || resource->second->resource_list != MEM_NULL);
}

const RendResVK& RendResViewVK::get(std::string_view p_resource_id) const {
    const std::string_view graph_resource_id = resolve(p_resource_id);
    auto resource = resources->find(graph_resource_id);
    if (resource == resources->end() || resource->second == MEM_NULL ||
        (resource->second->container == MEM_NULL && resource->second->resource_list == MEM_NULL)) {
        if (graph_resource_id != p_resource_id) {
            throw std::runtime_error(
                std::format("Render graph resource was not declared: {} (mapped from {}).", graph_resource_id, p_resource_id));
        }
        throw std::runtime_error(std::format("Render graph resource was not declared: {}.", p_resource_id));
    }
    return *resource->second;
}

DebugDataRows RendResViewVK::descriptor_debug_data() const {
    RenderAllocator* allocator = resources->get_allocator().allocator;
    DebugDataRows rows(allocator);
    for (const auto& [resource_id, resource] : *resources) {
        if (resource == MEM_NULL || resource->container == MEM_NULL) {
            continue;
        }
        DebugDataRows resource_rows = resource->container->descriptor_debug_data();
        for (DebugData& row : resource_rows) {
            row.set_value("resource_id", resource_id);
            rows.emplace_back(std::move(row));
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DebugData& p_left, const DebugData& p_right) {
        return std::make_tuple(descriptor_heap_index(p_left),
                   descriptor_heap(p_left),
                   descriptor_resource_id(p_left),
                   descriptor_slot_index(p_left),
                   descriptor_type_name(p_left)) < std::make_tuple(descriptor_heap_index(p_right),
                                                       descriptor_heap(p_right),
                                                       descriptor_resource_id(p_right),
                                                       descriptor_slot_index(p_right),
                                                       descriptor_type_name(p_right));
    });
    return rows;
}

Ref<RGNode> RenderGraphVK::add_node(Unique<IRenderTaskVK> p_task) {
    auto node = make_ref<RGNode>(render_data->allocator, render_data->allocator);
    node->render_task = std::move(p_task);
    node->in_queue = node->render_task->queue_family();
    nodes.emplace_back(node);
    return node;
}

Ref<RGNode> RenderGraphVK::find_node(std::string_view p_task_id) const {
    for (Ref<RGNode> node : nodes) {
        if (node->task_id == p_task_id) {
            return node;
        }
    }
    return MEM_NULL;
}

void RenderGraphVK::add_resource_edge(Ref<RGNode> p_from, Ref<RGNode> p_to, std::string p_resource_id) {
    EdgeScopeVK src_scope = node_resource_scope(*p_from, p_resource_id);
    EdgeScopeVK dst_scope = node_resource_scope(*p_to, p_resource_id);
    auto ensure_accessed = [&](const EdgeScopeVK& p_scope, const RGNode& p_node) {
        if (p_scope.stage == VK_PIPELINE_STAGE_2_NONE) {
            throw std::runtime_error(std::format("RenderGraphVK: edge '{}' -> '{}' carries resource '{}', but task '{}' does not access it.",
                p_from->task_id,
                p_to->task_id,
                p_resource_id,
                p_node.task_id));
        }
    };
    ensure_accessed(src_scope, *p_from);
    ensure_accessed(dst_scope, *p_to);
    auto edge = make_ref<RGResEdge>(render_data->allocator);
    edge->from = p_from;
    edge->to = std::move(p_to);
    edge->res_id = std::move(p_resource_id);
    // Only the previous task's writes need to be made available; a previous read only needs an execution dependency.
    edge->src_stage = src_scope.stage;
    edge->src_access = src_scope.write_access;
    edge->dst_stage = dst_scope.stage;
    edge->dst_access = dst_scope.read_access | dst_scope.write_access;
    p_from->out_edges.emplace_back(edge);
    resource_edges.emplace_back(std::move(edge));
}

void RenderGraphVK::topological_sort() { // NOLINT
    // Kahn's algorithm over resource edges. Parallel edges between the same pair of nodes each count once.
    Vector<size_t, RenderAllocator> in_degree(nodes.size(), 0, render_data->allocator);
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i]->topo_order = i;
    }
    for (const Ref<RGResEdge>& edge : resource_edges) {
        ++in_degree[edge->to->topo_order];
    }
    Vector<Ref<RGNode>, RenderAllocator> sorted(render_data->allocator);
    sorted.reserve(nodes.size());
    Vector<size_t, RenderAllocator> ready(render_data->allocator);
    // Seed in reverse so that independent tasks keep their descriptor order.
    for (size_t i = nodes.size(); i > 0; --i) {
        if (in_degree[i - 1] == 0) {
            ready.push_back(i - 1);
        }
    }
    while (!ready.empty()) {
        size_t node_index = ready.back();
        ready.pop_back();
        Ref<RGNode> node = nodes[node_index];
        sorted.push_back(node);
        for (auto& weak_edge : node->out_edges) {
            Ref<RGResEdge> edge = weak_edge.lock();
            WBE_DEBUG_ASSERT(edge != MEM_NULL);
            size_t consumer_index = edge->to->topo_order;
            --in_degree[consumer_index];
            if (in_degree[consumer_index] == 0) {
                ready.push_back(consumer_index);
            }
        }
    }
    if (sorted.size() != nodes.size()) {
        std::string cyclic_tasks;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (in_degree[i] > 0) {
                cyclic_tasks += std::format("{}'{}'", cyclic_tasks.empty() ? "" : ", ", nodes[i]->task_id);
            }
        }
        throw std::runtime_error(std::format("RenderGraphVK: render graph contains a cycle; unresolved tasks: {}.", cyclic_tasks));
    }
    nodes = std::move(sorted);
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i]->topo_order = i;
    }
}

void RenderGraphVK::allocate_command_buffers() {
    const uint32_t frames = std::max<uint32_t>(render_data->frames_in_flight, 1U);
    const size_t total = static_cast<size_t>(frames) * nodes.size();
    command_buffers.assign(total, VK_NULL_HANDLE);
    command_buffer_pools.assign(total, VK_NULL_HANDLE);
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto queue_family = nodes[i]->in_queue;
        VkCommandPool command_pool = pool_for(render_data, queue_family);
        if (command_pool == VK_NULL_HANDLE) {
            throw std::runtime_error(
                std::format("Failed to create command pool for queue family: {}.", std::to_underlying(queue_family)));
        }
        for (uint32_t f = 0; f < frames; ++f) {
            const size_t slot = static_cast<size_t>(f) * nodes.size() + i;
            command_buffer_pools[slot] = command_pool;
            VkCommandBufferAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.commandPool = command_pool;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(render_data->logic_device, &alloc_info, &command_buffers[slot]) != VK_SUCCESS) {
                throw std::runtime_error("RenderGraphVK: failed to allocate command buffer.");
            }
        }
    }
}

void RenderGraphVK::free_command_buffers() {
    const bool has_allocated_command_buffer = std::ranges::any_of(command_buffers, [](VkCommandBuffer p_command_buffer) {
        return p_command_buffer != VK_NULL_HANDLE;
    });
    if (has_allocated_command_buffer) {
        vkDeviceWaitIdle(render_data->logic_device);
    }
    for (size_t i = 0; i < command_buffers.size(); ++i) {
        if (command_buffers[i] != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(render_data->logic_device, command_buffer_pools[i], 1, &command_buffers[i]);
            command_buffers[i] = VK_NULL_HANDLE;
        }
    }
    command_buffers.clear();
    command_buffer_pools.clear();
}

uint32_t RenderGraphVK::terminal_semaphore_index() const {
    if (terminal_semaphores.empty()) {
        throw std::runtime_error("RenderGraphVK: terminal semaphores are not allocated.");
    }
    if (render_data->current_swap_chain_index >= terminal_semaphores.size()) {
        throw std::runtime_error("RenderGraphVK: swap-chain image index exceeds terminal semaphore count.");
    }
    return render_data->current_swap_chain_index;
}

Ref<SemaphoreVK> RenderGraphVK::get_terminal_semaphore() const {
    return terminal_semaphores[terminal_semaphore_index()];
}

DebugData RenderGraphVK::list_descriptors() const {
    return make_descriptor_data(RendResViewVK(&all_resources).descriptor_debug_data());
}

RendGraphMetadataVK RenderGraphVK::dump_graph_metadata() const {
    auto node_writes = [](const RGNode& p_node, std::string_view p_resource_id) {
        return std::ranges::any_of(p_node.writes, [p_resource_id](const ResAccessVK& p_access) {
            return p_access.res_id == p_resource_id;
        });
    };
    RendGraphMetadataVK metadata(render_data->allocator);
    // Nested subgraph ids extend the ids of the subgraphs containing them, so the innermost subgraph containing a task, or
    // a subgraph, is the one with the longest id among those that contain it.
    const auto& subgraphs = rebuild_state->subgraphs;
    const auto innermost = [&subgraphs](const auto& p_contains) -> RendTaskID {
        const RendSubgraphVK* result = nullptr;
        for (const RendSubgraphVK& subgraph : subgraphs) {
            if (p_contains(subgraph) && (result == nullptr || subgraph.subgraph_id.size() > result->subgraph_id.size())) {
                result = &subgraph;
            }
        }
        return result == nullptr ? RendTaskID() : result->subgraph_id;
    };
    metadata.tasks.reserve(nodes.size());
    // topological_sort() leaves the nodes in execution order.
    for (const auto& node : nodes) {
        const RendTaskID subgraph_id = innermost([&node](const RendSubgraphVK& p_subgraph) {
            return std::ranges::find(p_subgraph.members, node->task_id) != p_subgraph.members.end();
        });
        metadata.tasks.push_back(
            {.task_id = node->task_id, .queue_family = node->in_queue, .topological_order = node->topo_order, .subgraph_id = subgraph_id});
    }
    // A subgraph is added after the subgraphs nested in it, so walking the records backwards lists every parent first.
    metadata.subgraphs.reserve(subgraphs.size());
    for (auto subgraph = subgraphs.rbegin(); subgraph != subgraphs.rend(); ++subgraph) {
        const RendTaskID parent_id = innermost([&subgraph](const RendSubgraphVK& p_candidate) {
            return subgraph->subgraph_id.starts_with(std::format("{}.", p_candidate.subgraph_id));
        });
        metadata.subgraphs.push_back({.subgraph_id = subgraph->subgraph_id, .parent_subgraph_id = parent_id});
    }
    metadata.edges.reserve(resource_edges.size());
    for (const auto& edge : resource_edges) {
        metadata.edges.push_back({
            .from_task_id = edge->from->task_id,
            .to_task_id = edge->to->task_id,
            .resource_id = edge->res_id,
            .src_stage = edge->src_stage,
            .src_access = edge->src_access,
            .dst_stage = edge->dst_stage,
            .dst_access = edge->dst_access,
            .from_task_writes = node_writes(*edge->from, edge->res_id),
            .to_task_writes = node_writes(*edge->to, edge->res_id),
            .cross_queue = edge->from->in_queue != edge->to->in_queue,
        });
    }
    return metadata;
}

void RenderGraphVK::build_from_callback(RendGraphRebuildCallbackVK p_rebuild) {
    if (p_rebuild == nullptr) {
        throw std::runtime_error("RenderGraphVK: the rebuild callback is null.");
    }
    free_command_buffers();
    nodes.clear();
    resource_edges.clear();
    all_resources.clear();
    rebuild_state->signatures.clear();
    rebuild_state->owned_containers.clear();
    rebuild_state->task_descs.clear();
    rebuild_state->callback = std::move(p_rebuild);

    context = RendGraphContextVK::from_render_data(*render_data);
    apply_descriptor(rebuild_state->callback(context));

    // Allocate one terminal binary semaphore per swap-chain image. Presentation
    // may retain the wait semaphore until that exact image is acquired again.
    const uint32_t swap_chain_images = std::max<uint32_t>(render_data->swap_chain_images_count, 1U);
    terminal_semaphores.clear();
    terminal_semaphores.reserve(swap_chain_images);
    for (uint32_t i = 0; i < swap_chain_images; ++i) {
        terminal_semaphores.emplace_back(make_ref<SemaphoreVK>(render_data->allocator, render_data, true));
    }
}

Map<std::string, Ref<RendResVK>, RenderAllocator> RenderGraphVK::index_resources(const RendGraphDescVK& p_desc) {
    Map<std::string, Ref<RendResVK>, RenderAllocator> resources_by_id(render_data->allocator);
    resources_by_id.reserve(p_desc.resources.size());
    for (const auto& resource_desc : p_desc.resources) {
        if (resource_desc.res_id.empty()) {
            throw std::runtime_error("RenderGraphVK: resource id is empty.");
        }
        if (resource_desc.resource_list != MEM_NULL) {
            if (resource_desc.container != MEM_NULL) {
                throw std::runtime_error(
                    std::format("RenderGraphVK: aggregate resource '{}' also has a concrete container.", resource_desc.res_id));
            }
            register_resource_list(&resources_by_id, render_data->allocator, resource_desc.res_id, resource_desc.resource_list);
            continue;
        }
        if (resource_desc.container == MEM_NULL) {
            throw std::runtime_error(
                std::format("RenderGraphVK: resource '{}' must be bound before graph build.", resource_desc.res_id));
        }
        if (resource_desc.container->get_resource_type_id() != resource_desc.res_type_id) {
            throw std::runtime_error(
                std::format("RenderGraphVK: resource container type does not match declaration for '{}'.", resource_desc.res_id));
        }
        if (resources_by_id.contains(resource_desc.res_id)) {
            throw std::runtime_error(std::format("RenderGraphVK: duplicate resource id: {}.", resource_desc.res_id));
        }
        auto resource_ref = make_ref<RendResVK>(render_data->allocator);
        resource_ref->res_id = resource_desc.res_id;
        resource_ref->res_type_id = resource_desc.res_type_id;
        resource_ref->container = resource_desc.container;
        resources_by_id.emplace(resource_desc.res_id, std::move(resource_ref));
    }
    return resources_by_id;
}

void RenderGraphVK::register_resource(std::string_view p_resource_id, Ref<RenderObjectContainerVK> p_container) {
    if (all_resources.contains(p_resource_id)) {
        throw std::runtime_error(std::format("RenderGraphVK: duplicate resource id: {}.", p_resource_id));
    }
    auto resource = make_ref<RendResVK>(render_data->allocator);
    resource->res_id = p_resource_id;
    resource->res_type_id = p_container->get_resource_type_id();
    resource->container = std::move(p_container);
    all_resources.emplace(std::string(p_resource_id), std::move(resource));
}

Ref<RenderObjectContainerVK> RenderGraphVK::create_container(
    std::string_view p_resource_id, bool p_per_frame, RenderObjectContainerVK::Factory p_factory) const {
    RenderObjectContainerVK::Factory factory = std::move(p_factory);
    if (!resource_key.empty() && render_data->render_database != nullptr) {
        // Register every object the container creates, including ones recreated after the context changes, as
        // `<graph key>.<id>`, or `<graph key>.<id>[<slot>]` for each frame's copy; registering names the object.
        factory = [this, inner = std::move(factory), key = std::format("{}.{}", resource_key, p_resource_id), p_per_frame](uint32_t p_slot) {
            Ref<IRenderObjectVK> result = inner(p_slot);
            if (result != MEM_NULL) {
                render_data->render_database->register_compiled_render_resource(p_per_frame ? std::format("{}[{}]", key, p_slot) : key, result);
            }
            return result;
        };
    }
    if (p_per_frame) {
        const uint32_t frames = std::max<uint32_t>(render_data->frames_in_flight, 1U);
        return make_ref<ROCMultislotsVK>(render_data->allocator, render_data->allocator, frames, std::move(factory));
    }
    return make_ref<ROCSingleVK>(render_data->allocator, std::move(factory));
}

void RenderGraphVK::apply_descriptor(const RendGraphDescVK& p_desc) {
    Map<std::string, Ref<RendResVK>, RenderAllocator> imports = index_resources(p_desc);
    validate_graph_resource_ids(p_desc, imports);
    RendGraphResourceDiffVK::Signatures next = evaluate_signatures(p_desc, imports, context, render_data->allocator);
    const RendGraphResourceDiffVK::Actions actions = RendGraphResourceDiffVK::plan(rebuild_state->signatures, next, render_data->allocator);

    // The new configuration is valid; switch to it. Containers create their objects from these signatures.
    rebuild_state->signatures = std::move(next);
    all_resources = std::move(imports);
    auto& owned_containers = rebuild_state->owned_containers;
    for (auto container = owned_containers.begin(); container != owned_containers.end();) {
        const auto signature = rebuild_state->signatures.find(container->first);
        if (signature == rebuild_state->signatures.end() || signature->second.kind == RendResKindVK::IMPORT) {
            container = owned_containers.erase(container);
        } else {
            ++container;
        }
    }
    // Views come after images, because they view the objects of their image's container.
    for (const RendBufferDeclVK& buffer_decl : p_desc.buffers) {
        apply_owned_resource(buffer_decl.res_id, actions.find(buffer_decl.res_id)->second);
    }
    for (const RendImageDeclVK& image_decl : p_desc.images) {
        apply_owned_resource(image_decl.res_id, actions.find(image_decl.res_id)->second);
    }
    for (const RendImageViewDeclVK& view_decl : p_desc.image_views) {
        apply_owned_resource(view_decl.res_id, actions.find(view_decl.res_id)->second);
    }
    for (const RendSamplerDeclVK& sampler_decl : p_desc.samplers) {
        apply_owned_resource(sampler_decl.res_id, actions.find(sampler_decl.res_id)->second);
    }
    rebuild_state->task_descs = p_desc.tasks;
    rebuild_state->subgraphs = p_desc.subgraphs;
    build_tasks(rebuild_state->task_descs, rebuild_state->subgraphs);
}

void RenderGraphVK::apply_owned_resource(std::string_view p_resource_id, RendResRebuildActionVK p_action) {
    const auto signature = rebuild_state->signatures.find(p_resource_id);
    WBE_DEBUG_ASSERT(signature != rebuild_state->signatures.end());
    auto owned = rebuild_state->owned_containers.find(p_resource_id);
    Ref<RenderObjectContainerVK> container;
    if (p_action == RendResRebuildActionVK::CREATE || p_action == RendResRebuildActionVK::REPLACE ||
        owned == rebuild_state->owned_containers.end()) {
        container = create_container(p_resource_id,
            signature->second.per_frame,
            [this, resource_id = std::string(p_resource_id)](uint32_t p_slot) -> Ref<IRenderObjectVK> {
                return create_owned_object(resource_id, p_slot);
            });
        rebuild_state->owned_containers.insert_or_assign(std::string(p_resource_id), container);
    } else {
        container = owned->second;
        bool recreate = p_action == RendResRebuildActionVK::RECREATE;
        if (!recreate && signature->second.kind == RendResKindVK::IMAGE_VIEW) {
            const auto image = all_resources.find(signature->second.image_id);
            WBE_DEBUG_ASSERT(image != all_resources.end());
            recreate = view_image_changed(container, image->second->container);
        }
        if (recreate) {
            container->recreate();
        }
    }
    register_resource(p_resource_id, std::move(container));
}

Ref<IRenderObjectVK> RenderGraphVK::create_owned_object(std::string_view p_resource_id, uint32_t p_slot) const {
    const auto signature = rebuild_state->signatures.find(p_resource_id);
    WBE_DEBUG_ASSERT(signature != rebuild_state->signatures.end());
    const RendResSignatureVK& resource = signature->second;
    switch (resource.kind) {
    case RendResKindVK::BUFFER:
        return make_buffer(render_data, p_resource_id, std::get<RendBufferDescVK>(resource.desc));
    case RendResKindVK::IMAGE:
        return make_image(render_data, p_resource_id, std::get<RendImageDescVK>(resource.desc));
    case RendResKindVK::IMAGE_VIEW: {
        const auto image_resource = all_resources.find(resource.image_id);
        WBE_DEBUG_ASSERT(image_resource != all_resources.end() && image_resource->second->container != MEM_NULL);
        Ref<ROImageVK> image = image_resource->second->container->get(p_slot).dynamic_cast_ref<ROImageVK>();
        WBE_DEBUG_ASSERT(image != MEM_NULL);
        return make_image_view(image, p_resource_id, resource.image_id, std::get<RendImageViewDescVK>(resource.desc));
    }
    case RendResKindVK::SAMPLER:
        return render_data->create_sampler(make_sampler_builder(std::get<RendSamplerDescVK>(resource.desc)));
    case RendResKindVK::IMPORT:
        break;
    }
    throw std::runtime_error(std::format("RenderGraphVK: resource '{}' is not owned by the graph.", p_resource_id));
}

void RenderGraphVK::update_context(const RendGraphContextVK& p_context) {
    if (p_context == context) {
        return;
    }
    context = p_context;
    rebuild();
}

void RenderGraphVK::rebuild() {
    if (rebuild_state->callback == nullptr) {
        throw std::runtime_error("RenderGraphVK: cannot rebuild a graph that was not built from a callback.");
    }
    apply_descriptor(rebuild_state->callback(context));
}

Ref<RenderObjectContainerVK> RenderGraphVK::acquire_resource(std::string_view p_resource_id) const {
    auto resource = all_resources.find(p_resource_id);
    if (resource == all_resources.end() || resource->second == MEM_NULL || resource->second->container == MEM_NULL) {
        throw std::runtime_error(std::format("RenderGraphVK: resource '{}' is not declared or has no container.", p_resource_id));
    }
    return resource->second->container;
}

void RenderGraphVK::build_tasks(
    const Vector<RendTaskDescVK, RenderAllocator>& p_tasks, const Vector<RendSubgraphVK, RenderAllocator>& p_subgraphs) {
    free_command_buffers();
    nodes.clear();
    resource_edges.clear();

    instantiate_tasks(p_tasks);
    // Subgraph edges resolve against the accesses the tasks have just reported.
    if (p_subgraphs.empty()) {
        add_resource_edges(p_tasks);
    } else {
        add_resource_edges(resolve_subgraph_edges(p_tasks, p_subgraphs));
    }

    topological_sort();
    validate_resource_conflicts();
    validate_queue_completion();
    build_synchronization();

    allocate_command_buffers();
}

void RenderGraphVK::instantiate_tasks(const Vector<RendTaskDescVK, RenderAllocator>& p_tasks) {
    Vector<RendTaskID, RenderAllocator> task_ids(render_data->allocator);
    task_ids.reserve(p_tasks.size());
    for (const auto& task_desc : p_tasks) {
        if (task_desc.task_id.empty()) {
            throw std::runtime_error("RenderGraphVK: task id is empty.");
        }
        if (std::find(task_ids.begin(), task_ids.end(), task_desc.task_id) != task_ids.end()) {
            throw std::runtime_error(std::format("RenderGraphVK: duplicate task id: {}.", task_desc.task_id));
        }
        task_ids.emplace_back(task_desc.task_id);
    }
    for (const auto& task_desc : p_tasks) {
        if (task_desc.factory == nullptr) {
            throw std::runtime_error(std::format("RenderGraphVK: task factory is missing for: {}.", task_desc.task_id));
        }
        Unique<IRenderTaskVK> task = task_desc.factory(context);
        if (task.is_null()) {
            throw std::runtime_error(std::format("RenderGraphVK: task factory returned null for: {}.", task_desc.task_id));
        }
        Ref<RGNode> node = add_node(std::move(task));
        node->task_id = task_desc.task_id;
        node->resource_map = task_desc.resource_map;
        node->render_task->bind_resources(RendResViewVK(&all_resources, &node->resource_map));
        node->render_task->append_accesses(node->reads, node->writes);
        normalize_task_accesses(node);
        node->resource_map.validate_all_used(node->task_id);
        for (const ResAccessVK& access : node->reads) {
            validate_declared_resource(all_resources, *node, access, false);
        }
        for (const ResAccessVK& access : node->writes) {
            validate_declared_resource(all_resources, *node, access, true);
        }
    }
}

void RenderGraphVK::normalize_task_accesses(Ref<RGNode> p_node) const {
    // Resolve task-local identifiers through the task's resource map, then expand aggregate resource lists.
    auto normalize = [this, &p_node](Vector<ResAccessVK, RenderAllocator>& p_accesses) {
        Vector<ResAccessVK, RenderAllocator> normalized(render_data->allocator);
        for (const ResAccessVK& access : p_accesses) {
            for (std::string& resource_id : expand_resource_id(p_node->resource_map.resolve(access.res_id))) {
                normalized.push_back({.res_id = std::move(resource_id), .usage = access.usage});
            }
        }
        p_accesses = std::move(normalized);
    };
    normalize(p_node->reads);
    normalize(p_node->writes);
}

Vector<std::string, RenderAllocator> RenderGraphVK::expand_resource_id(std::string_view p_resource_id) const {
    Vector<std::string, RenderAllocator> result(render_data->allocator);
    auto resource = all_resources.find(p_resource_id);
    if (resource == all_resources.end() || resource->second == MEM_NULL || resource->second->resource_list == MEM_NULL) {
        result.emplace_back(p_resource_id);
        return result;
    }
    for (size_t index = 0; index < resource->second->resource_list->get_resource_count(); ++index) {
        result.push_back(std::format("{}[{}]", p_resource_id, index));
    }
    return result;
}

Vector<RendTaskDescVK, RenderAllocator> RenderGraphVK::resolve_subgraph_edges(
    const Vector<RendTaskDescVK, RenderAllocator>& p_tasks, const Vector<RendSubgraphVK, RenderAllocator>& p_subgraphs) const {
    for (const RendSubgraphVK& subgraph : p_subgraphs) {
        if (find_node(subgraph.subgraph_id) != MEM_NULL) {
            throw std::runtime_error(std::format("RenderGraphVK: subgraph '{}' has the id of a task.", subgraph.subgraph_id));
        }
    }
    const auto find_subgraph = [&p_subgraphs](std::string_view p_id) -> const RendSubgraphVK* {
        for (const RendSubgraphVK& subgraph : p_subgraphs) {
            if (subgraph.subgraph_id == p_id) {
                return &subgraph;
            }
        }
        return nullptr;
    };
    const auto find_task = [&p_tasks](std::string_view p_id) -> const RendTaskDescVK* {
        for (const RendTaskDescVK& task : p_tasks) {
            if (task.task_id == p_id) {
                return &task;
            }
        }
        return nullptr;
    };
    // Whether a task accesses a resource, or an element of the resource list it names.
    const auto accesses = [this](std::string_view p_task_id, std::string_view p_resource_id) {
        const Ref<RGNode> node = find_node(p_task_id);
        WBE_DEBUG_ASSERT(node != MEM_NULL);
        const std::string element_prefix = std::format("{}[", p_resource_id);
        const auto matches = [&](const ResAccessVK& p_access) {
            return p_access.res_id == p_resource_id || p_access.res_id.starts_with(element_prefix);
        };
        return std::ranges::any_of(node->reads, matches) || std::ranges::any_of(node->writes, matches);
    };
    using TaskIDs = Vector<RendTaskID, RenderAllocator>;
    // The members at the boundary of a subgraph on a resource: the entries without an edge from another member, or the exits
    // without an edge to another member.
    std::function<TaskIDs(const RendSubgraphVK&, std::string_view, bool)> boundary;
    // The tasks an edge to p_id on the resource reaches.
    const auto edge_targets = [&](std::string_view p_id, std::string_view p_resource_id) {
        if (const RendSubgraphVK* subgraph = find_subgraph(p_id)) {
            return boundary(*subgraph, p_resource_id, true);
        }
        TaskIDs result(render_data->allocator);
        result.emplace_back(p_id);
        return result;
    };
    // The boundaries being resolved; resolving one again means the subgraph edges form a cycle.
    Vector<std::string, RenderAllocator> resolving(render_data->allocator);
    boundary = [&](const RendSubgraphVK& p_subgraph, std::string_view p_resource_id, bool p_entries) {
        const std::string key = std::format("{} {} {}", p_subgraph.subgraph_id, p_resource_id, p_entries ? "entries" : "exits");
        if (std::ranges::find(resolving, key) != resolving.end()) {
            throw std::runtime_error(std::format(
                "RenderGraphVK: the edges of subgraph '{}' on resource '{}' form a cycle.", p_subgraph.subgraph_id, p_resource_id));
        }
        resolving.push_back(key);
        const auto is_member = [&p_subgraph](std::string_view p_id) {
            return std::ranges::find(p_subgraph.members, p_id) != p_subgraph.members.end();
        };
        TaskIDs linked(render_data->allocator);
        const auto add_internal_edges = [&](const TaskIDs& p_sources, const Vector<RendResEdgeDescVK, RenderAllocator>& p_edges) {
            for (const RendResEdgeDescVK& edge : p_edges) {
                if (edge.res_id != p_resource_id) {
                    continue;
                }
                for (const RendTaskID& target : edge_targets(edge.next_task_id, p_resource_id)) {
                    if (!is_member(target)) {
                        continue;
                    }
                    if (p_entries) {
                        linked.push_back(target);
                    } else {
                        linked.insert(linked.end(), p_sources.begin(), p_sources.end());
                    }
                }
            }
        };
        for (const RendTaskID& member : p_subgraph.members) {
            const RendTaskDescVK* task = find_task(member);
            WBE_DEBUG_ASSERT(task != nullptr);
            TaskIDs source(render_data->allocator);
            source.push_back(member);
            add_internal_edges(source, task->next);
        }
        const std::string nested_prefix = std::format("{}.", p_subgraph.subgraph_id);
        for (const RendSubgraphVK& nested : p_subgraphs) {
            if (nested.subgraph_id.starts_with(nested_prefix)) {
                bool uses_resource = std::ranges::any_of(nested.next, [p_resource_id](const RendResEdgeDescVK& p_edge) {
                    return p_edge.res_id == p_resource_id;
                });
                if (uses_resource) {
                    add_internal_edges(boundary(nested, p_resource_id, false), nested.next);
                }
            }
        }
        TaskIDs result(render_data->allocator);
        for (const RendTaskID& member : p_subgraph.members) {
            if (accesses(member, p_resource_id) && std::ranges::find(linked, member) == linked.end()) {
                result.push_back(member);
            }
        }
        if (result.empty()) {
            throw std::runtime_error(std::format(
                "RenderGraphVK: subgraph '{}' is connected on resource '{}', but none of its tasks accesses it.", p_subgraph.subgraph_id, p_resource_id));
        }
        resolving.pop_back();
        return result;
    };

    Vector<RendTaskDescVK, RenderAllocator> result = p_tasks;
    const auto add_edge = [](RendTaskDescVK& p_task, const RendTaskID& p_next_task_id, const std::string& p_resource_id) {
        const bool exists = std::ranges::any_of(p_task.next, [&](const RendResEdgeDescVK& p_edge) {
            return p_edge.next_task_id == p_next_task_id && p_edge.res_id == p_resource_id;
        });
        if (!exists) {
            p_task.next.push_back({.next_task_id = p_next_task_id, .res_id = p_resource_id});
        }
    };
    const auto find_result_task = [&result](std::string_view p_id) -> RendTaskDescVK& {
        const auto task = std::ranges::find_if(result, [p_id](const RendTaskDescVK& p_task) {
            return p_task.task_id == p_id;
        });
        WBE_DEBUG_ASSERT(task != result.end());
        return *task;
    };
    for (RendTaskDescVK& task : result) {
        task.next.clear();
    }
    for (const RendTaskDescVK& task : p_tasks) {
        RendTaskDescVK& resolved = find_result_task(task.task_id);
        for (const RendResEdgeDescVK& edge : task.next) {
            for (const RendTaskID& target : edge_targets(edge.next_task_id, edge.res_id)) {
                add_edge(resolved, target, edge.res_id);
            }
        }
    }
    for (const RendSubgraphVK& subgraph : p_subgraphs) {
        for (const RendResEdgeDescVK& edge : subgraph.next) {
            const TaskIDs targets = edge_targets(edge.next_task_id, edge.res_id);
            for (const RendTaskID& source : boundary(subgraph, edge.res_id, false)) {
                for (const RendTaskID& target : targets) {
                    add_edge(find_result_task(source), target, edge.res_id);
                }
            }
        }
    }
    return result;
}

void RenderGraphVK::add_resource_edges(const Vector<RendTaskDescVK, RenderAllocator>& p_tasks) {
    for (const RendTaskDescVK& task_desc : p_tasks) {
        Ref<RGNode> from = find_node(task_desc.task_id);
        WBE_DEBUG_ASSERT(from != MEM_NULL);
        for (const RendResEdgeDescVK& edge_desc : task_desc.next) {
            Ref<RGNode> to = find_node(edge_desc.next_task_id);
            if (to == MEM_NULL) {
                throw std::runtime_error(std::format(
                    "RenderGraphVK: task '{}' connects to unknown task '{}'.", task_desc.task_id, edge_desc.next_task_id));
            }
            if (to == from) {
                throw std::runtime_error(std::format(
                    "RenderGraphVK: task '{}' connects to itself on resource '{}'.", task_desc.task_id, edge_desc.res_id));
            }
            if (!all_resources.contains(edge_desc.res_id)) {
                throw std::runtime_error(std::format("RenderGraphVK: edge '{}' -> '{}' uses undeclared resource '{}'.",
                    task_desc.task_id,
                    edge_desc.next_task_id,
                    edge_desc.res_id));
            }
            for (std::string& resource_id : expand_resource_id(edge_desc.res_id)) {
                add_resource_edge(from, to, std::move(resource_id));
            }
        }
    }
}

bool RenderGraphVK::has_resource_path(Ref<RGNode> p_from, Ref<RGNode> p_to, std::string_view p_resource_id) const {
    Vector<uint8_t, RenderAllocator> visited(nodes.size(), 0, render_data->allocator);
    Vector<Ref<RGNode>, RenderAllocator> stack(render_data->allocator);
    stack.push_back(p_from);
    visited[p_from->topo_order] = 1;
    while (!stack.empty()) {
        Ref<RGNode> node = stack.back();
        stack.pop_back();
        for (auto& weak_edge : node->out_edges) {
            Ref<RGResEdge> edge = weak_edge.lock();
            WBE_DEBUG_ASSERT(edge != MEM_NULL);
            if (edge->res_id != p_resource_id) {
                continue;
            }
            if (edge->to == p_to) {
                return true;
            }
            if (visited[edge->to->topo_order] != 0) {
                continue;
            }
            visited[edge->to->topo_order] = 1;
            stack.push_back(edge->to);
        }
    }
    return false;
}

void RenderGraphVK::validate_resource_conflicts() const {
    struct NodeAccess {
        Ref<RGNode> node;
        bool writes = false;
    };
    Map<std::string, Vector<NodeAccess, RenderAllocator>, RenderAllocator> accesses_by_resource(render_data->allocator);
    for (Ref<RGNode> node : nodes) {
        auto add_access = [&](const ResAccessVK& p_access, bool p_is_write) {
            auto [entry, inserted] = accesses_by_resource.try_emplace(p_access.res_id, render_data->allocator);
            Vector<NodeAccess, RenderAllocator>& accesses = entry->second;
            if (!accesses.empty() && accesses.back().node == node) {
                accesses.back().writes = accesses.back().writes || p_is_write;
                return;
            }
            auto existing = std::ranges::find_if(accesses, [&node](const NodeAccess& p_node_access) {
                return p_node_access.node == node;
            });
            if (existing != accesses.end()) {
                existing->writes = existing->writes || p_is_write;
                return;
            }
            accesses.push_back({.node = node, .writes = p_is_write});
        };
        for (const ResAccessVK& access : node->reads) {
            add_access(access, false);
        }
        for (const ResAccessVK& access : node->writes) {
            add_access(access, true);
        }
    }
    // Nodes are topologically sorted and were visited in order, so every access list is in execution order.
    for (const auto& [resource_id, accesses] : accesses_by_resource) {
        for (size_t before = 0; before < accesses.size(); ++before) {
            for (size_t after = before + 1; after < accesses.size(); ++after) {
                if (!accesses[before].writes && !accesses[after].writes) {
                    // Both accesses are reads, no conflict.
                    continue;
                }
                if (!has_resource_path(accesses[before].node, accesses[after].node, resource_id)) {
                    throw std::runtime_error(std::format("RenderGraphVK: tasks '{}' and '{}' both access resource '{}' without a chain of edges on that resource.",
                        accesses[before].node->task_id,
                        accesses[after].node->task_id,
                        resource_id));
                }
            }
        }
    }
}

void RenderGraphVK::validate_queue_completion() const {
    // The in-flight fence is attached to the last graphics submission. Work on another queue is only covered by it when a
    // dependency path reaches a graphics task.
    Vector<uint8_t, RenderAllocator> reaches_graphics(nodes.size(), 0, render_data->allocator);
    bool has_graphics = false;
    for (size_t i = nodes.size(); i > 0; --i) {
        Ref<RGNode> node = nodes[i - 1];
        if (node->in_queue == QueueFamilyVK::GRAPHICS) {
            has_graphics = true;
            reaches_graphics[i - 1] = 1;
            continue;
        }
        for (auto& weak_edge : node->out_edges) {
            Ref<RGResEdge> edge = weak_edge.lock();
            WBE_DEBUG_ASSERT(edge != MEM_NULL);
            if (reaches_graphics[edge->to->topo_order] != 0) {
                reaches_graphics[i - 1] = 1;
                break;
            }
        }
    }
    if (!has_graphics) {
        return;
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (reaches_graphics[i] == 0 && !is_same_queue(nodes[i]->in_queue, QueueFamilyVK::GRAPHICS)) {
            throw std::runtime_error(std::format(
                "RenderGraphVK: task '{}' runs on another queue and has no dependency path to a graphics task.", nodes[i]->task_id));
        }
    }
}

bool RenderGraphVK::is_same_queue(QueueFamilyVK p_left, QueueFamilyVK p_right) const {
    return p_left == p_right || queue_for(render_data, p_left) == queue_for(render_data, p_right);
}

void RenderGraphVK::build_synchronization() {
    for (const Ref<RGResEdge>& edge : resource_edges) {
        Ref<RGNode> from = edge->from;
        Ref<RGNode> to = edge->to;
        if (is_same_queue(from->in_queue, to->in_queue)) {
            auto barrier = std::ranges::find_if(to->barriers, [&edge](const RGBarrierVK& p_barrier) {
                return p_barrier.res_id == edge->res_id;
            });
            if (barrier == to->barriers.end()) {
                to->barriers.push_back({.res_id = edge->res_id});
                barrier = to->barriers.end() - 1;
            }
            barrier->src_stage |= edge->src_stage;
            barrier->src_access |= edge->src_access;
            barrier->dst_stage |= edge->dst_stage;
            barrier->dst_access |= edge->dst_access;
            continue;
        }
        if (from->signal_semaphore == MEM_NULL) {
            from->signal_semaphore = make_ref<SemaphoreVK>(render_data->allocator, render_data, false);
        }
        from->signal_stage |= edge->src_stage;
        auto wait = std::ranges::find_if(to->semaphore_waits, [&from](const RGSemaphoreWaitVK& p_wait) {
            return p_wait.semaphore == from->signal_semaphore;
        });
        if (wait == to->semaphore_waits.end()) {
            to->semaphore_waits.push_back({.semaphore = from->signal_semaphore});
            wait = to->semaphore_waits.end() - 1;
        }
        wait->wait_stage |= edge->dst_stage;
    }
}

void RenderGraphVK::execute(uint64_t p_frame_index,
    double p_delta,
    std::span<const VkSemaphoreSubmitInfo> p_extra_waits_first_submit,
    std::span<const VkSemaphoreSubmitInfo> p_extra_signals_last_submit) {
    if (nodes.empty()) {
        return;
    }
    // The renderer waits for every previous frame before executing the next, so no submitted work uses the resources.
    update_context(RendGraphContextVK::from_render_data(*render_data));
    const uint64_t timeline_value = p_frame_index + 1;
    const auto [first_graphics_idx, last_graphics_idx] = find_graphics_range();

    // Each graph node owns one command buffer per frame slot and is submitted independently.
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto node = nodes[i];
        auto queue_family = node->in_queue;
        const uint32_t frame_slot = render_data->current_frame_slot;
        const size_t command_buffer_index = static_cast<size_t>(frame_slot) * nodes.size() + i;
        VkCommandBuffer command_buffer = command_buffers[command_buffer_index];

        if (vkResetCommandBuffer(command_buffer, 0) != VK_SUCCESS) {
            throw std::runtime_error("RenderGraphVK: failed to reset command buffer.");
        }

        RendResViewVK resource_view(&all_resources);
        const bool is_first_graphics = (i == first_graphics_idx && queue_family == QueueFamilyVK::GRAPHICS);
        const bool is_last_graphics = (i == last_graphics_idx && queue_family == QueueFamilyVK::GRAPHICS);
        // The first/last graphics nodes bridge external acquire/present synchronization.
        record_node(node, command_buffer, resource_view, p_delta, is_last_graphics);

        // Build wait/signal semaphore lists from cross-queue resource edges + extras.
        Vector<VkSemaphoreSubmitInfo, RenderAllocator> wait_infos(render_data->allocator);
        Vector<VkSemaphoreSubmitInfo, RenderAllocator> signal_infos(render_data->allocator);
        collect_node_semaphores(node, timeline_value, wait_infos, signal_infos);

        if (is_first_graphics) {
            for (const auto& wait_info : p_extra_waits_first_submit) {
                wait_infos.push_back(wait_info);
            }
        }
        if (is_last_graphics) {
            for (const auto& signal_info : p_extra_signals_last_submit) {
                signal_infos.push_back(signal_info);
            }
            // Also signal the terminal binary semaphore for this swap-chain image.
            signal_infos.push_back(get_terminal_semaphore()->get_submit_info(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
        }

        // Last graphics submit signals the in-flight fence too, so the renderer
        // may wait for the entire frame's GPU work to complete next time
        // around.
        VkFence fence = is_last_graphics ? render_data->in_flight_fence : VK_NULL_HANDLE;
        submit_node(command_buffer, queue_family, wait_infos, signal_infos, fence);
    }
}

std::tuple<size_t, size_t> RenderGraphVK::find_graphics_range() const {
    size_t first_index = nodes.size();
    size_t last_index = nodes.size();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]->in_queue == QueueFamilyVK::GRAPHICS) {
            if (first_index == nodes.size()) {
                first_index = i;
            }
            last_index = i;
        }
    }
    return {first_index, last_index};
}

void RenderGraphVK::record_node(
    Ref<RGNode> p_node, VkCommandBuffer p_command_buffer, const RendResViewVK& p_resources, double p_delta, bool p_record_present_transition) {
    // Record all barriers and task work for one node into its per-frame primary command buffer.
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(p_command_buffer, &begin_info) != VK_SUCCESS) {
        throw std::runtime_error("RenderGraphVK: failed to begin command buffer.");
    }
    if (render_data->render_plugins != nullptr) {
        for (auto& plugin : *render_data->render_plugins) {
            plugin->on_before_task_recorded(p_command_buffer, *this, p_node->task_id, p_node->in_queue);
        }
    }
    if (render_data->descriptor_heap_allocator == nullptr) {
        throw std::runtime_error("RenderGraphVK: descriptor heap allocator is unavailable.");
    }
    render_data->descriptor_heap_allocator->bind_heaps(
        p_command_buffer, render_data->resource_descriptor_heap_id, render_data->sampler_descriptor_heap_id);

    record_general_transition(p_command_buffer, p_node, p_resources);
    record_edge_barriers(p_command_buffer, p_node, p_resources);

    p_node->render_task->record(p_command_buffer, p_delta);

    if (p_record_present_transition) {
        // Present waits on this node's terminal semaphore, so the present layout transition must be recorded before it is signaled.
        record_present_transition(p_command_buffer);
    }

    if (render_data->render_plugins != nullptr) {
        for (auto& plugin : *render_data->render_plugins) {
            plugin->on_after_task_recorded(p_command_buffer, *this, p_node->task_id, p_node->in_queue);
        }
    }

    if (vkEndCommandBuffer(p_command_buffer) != VK_SUCCESS) {
        throw std::runtime_error("RenderGraphVK: failed to end command buffer.");
    }
}

void RenderGraphVK::record_general_transition(VkCommandBuffer p_command_buffer, Ref<RGNode> p_node, const RendResViewVK& p_resources) {
    // We're already using unified shader layout, but we still need an explicit transition out of UNDEFINED/PRESENT.
    QueueFamilyVK queue_family = node_queue_family(p_node);
    for (const auto& write : p_node->writes) {
        const auto& bound_resource = p_resources.get(write.res_id);
        auto image_view = resolve_render_object(bound_resource, render_data->current_frame_slot).dynamic_cast_ref<ROImageViewVK>();
        if (image_view == MEM_NULL || image_view->fields().image == MEM_NULL) {
            continue;
        }
        Ref<ROImageVK> image = image_view->fields().image;
        if (image->fields().image_layout == VK_IMAGE_LAYOUT_GENERAL) {
            // Filter out images that are already in general layout.
            continue;
        }
        ResourceScopeVK write_scope = resource_scope(write.usage, true, queue_family);
        // The source stage chains the transition after semaphore waits at the write stage, such as swap-chain acquisition.
        ROImageVK::MemoryBarrierBuilder()
            .set_src_access(VK_ACCESS_2_NONE, write_scope.stage)
            .set_dst_access(write_scope.access, write_scope.stage)
            .set_subresource_range(image_view->fields().subresource_range)
            .set_new_layout(VK_IMAGE_LAYOUT_GENERAL)
            .set_image(image)
            .apply_barrier(p_command_buffer);
    }
}

void RenderGraphVK::record_edge_barriers(VkCommandBuffer p_command_buffer, Ref<RGNode> p_node, const RendResViewVK& p_resources) const {
    if (p_node->barriers.empty()) {
        return;
    }
    VkMemoryBarrier2 memory_barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    Vector<VkBufferMemoryBarrier2, RenderAllocator> buffer_barriers(render_data->allocator);
    Vector<VkImageMemoryBarrier2, RenderAllocator> image_barriers(render_data->allocator);
    for (const RGBarrierVK& barrier : p_node->barriers) {
        Ref<IRenderObjectVK> resource = resolve_render_object(p_resources.get(barrier.res_id), render_data->current_frame_slot);
        if (auto buffer = resource.dynamic_cast_ref<ROBufferVK>(); buffer != MEM_NULL) {
            buffer_barriers.push_back(VkBufferMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = barrier.src_stage,
                .srcAccessMask = barrier.src_access,
                .dstStageMask = barrier.dst_stage,
                .dstAccessMask = barrier.dst_access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer->get_buffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE,
            });
            continue;
        }
        auto image_view = resource.dynamic_cast_ref<ROImageViewVK>();
        if (image_view != MEM_NULL && image_view->fields().image != MEM_NULL &&
            image_view->fields().image->fields().image_layout == VK_IMAGE_LAYOUT_GENERAL) {
            // Different views may alias the same image, so the barrier covers every mip level and layer.
            image_barriers.push_back(VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = barrier.src_stage,
                .srcAccessMask = barrier.src_access,
                .dstStageMask = barrier.dst_stage,
                .dstAccessMask = barrier.dst_access,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image_view->fields().image->get_image(),
                .subresourceRange =
                    VkImageSubresourceRange{
                        .aspectMask = image_view->fields().subresource_range.aspectMask,
                        .baseMipLevel = 0,
                        .levelCount = VK_REMAINING_MIP_LEVELS,
                        .baseArrayLayer = 0,
                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
                    },
            });
            continue;
        }
        // Other resources, and images outside the unified layout, are covered by a global memory barrier.
        memory_barrier.srcStageMask |= barrier.src_stage;
        memory_barrier.srcAccessMask |= barrier.src_access;
        memory_barrier.dstStageMask |= barrier.dst_stage;
        memory_barrier.dstAccessMask |= barrier.dst_access;
    }
    const bool has_memory_barrier = memory_barrier.srcStageMask != VK_PIPELINE_STAGE_2_NONE;
    VkDependencyInfo dependency_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = has_memory_barrier ? 1U : 0U,
        .pMemoryBarriers = has_memory_barrier ? &memory_barrier : nullptr,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size()),
        .pBufferMemoryBarriers = buffer_barriers.data(),
        .imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size()),
        .pImageMemoryBarriers = image_barriers.data(),
    };
    vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
}

void RenderGraphVK::record_present_transition(VkCommandBuffer p_command_buffer) {
    // WSI presentation still requires PRESENT_SRC_KHR even when unified image layouts are enabled.
    RendererVK* renderer = RendererVK::get_singleton();
    if (renderer == nullptr) {
        throw std::runtime_error("RenderGraphVK: renderer singleton is unavailable.");
    }
    Ref<ROCMultislotsVK> swap_chain_image_views = renderer->acquire_swap_chain_image_view_container();
    auto image_view = swap_chain_image_views->get(render_data->current_frame_slot).dynamic_cast_ref<ROImageViewVK>();
    if (image_view == MEM_NULL || image_view->fields().image == MEM_NULL) {
        throw std::runtime_error("RenderGraphVK: swap-chain image-view container did not resolve to an image view.");
    }

    Ref<ROImageVK> image = image_view->fields().image;
    if (!image->fields().is_swap_chain_image) {
        throw std::runtime_error("RenderGraphVK: swap-chain image resource does not reference a swap-chain image.");
    }

    // For the swapchain image, apply a barrier that transfers it into VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
    ROImageVK::MemoryBarrierBuilder()
        .set_src_access(VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
        .set_dst_access(VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE)
        .set_subresource_range(image_view->fields().subresource_range)
        .set_new_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        .set_image(image)
        .apply_barrier(p_command_buffer);
}

void RenderGraphVK::collect_node_semaphores(Ref<RGNode> p_node,
    uint64_t p_timeline_value,
    Vector<VkSemaphoreSubmitInfo, RenderAllocator>& p_waits,
    Vector<VkSemaphoreSubmitInfo, RenderAllocator>& p_signals) const {
    for (const RGSemaphoreWaitVK& wait : p_node->semaphore_waits) {
        p_waits.push_back(wait.semaphore->get_submit_info(wait.wait_stage, p_timeline_value));
    }
    if (p_node->signal_semaphore != MEM_NULL) {
        p_signals.push_back(p_node->signal_semaphore->get_submit_info(p_node->signal_stage, p_timeline_value));
    }
}

void RenderGraphVK::submit_node(VkCommandBuffer p_command_buffer,
    QueueFamilyVK p_queue_family,
    const Vector<VkSemaphoreSubmitInfo, RenderAllocator>& p_waits,
    const Vector<VkSemaphoreSubmitInfo, RenderAllocator>& p_signals,
    VkFence p_fence) {
    VkCommandBufferSubmitInfo command_buffer_submit_info{};
    command_buffer_submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_buffer_submit_info.commandBuffer = p_command_buffer;

    VkSubmitInfo2 submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = static_cast<uint32_t>(p_waits.size());
    submit_info.pWaitSemaphoreInfos = p_waits.data();
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &command_buffer_submit_info;
    submit_info.signalSemaphoreInfoCount = static_cast<uint32_t>(p_signals.size());
    submit_info.pSignalSemaphoreInfos = p_signals.data();

    VkQueue queue = queue_for(render_data, p_queue_family);
    auto result = vkQueueSubmit2(queue, 1, &submit_info, p_fence);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("RenderGraphVK: vkQueueSubmit2 failed: {}", string_VkResult(result)));
    }
}

} // namespace WhiteBirdEngine

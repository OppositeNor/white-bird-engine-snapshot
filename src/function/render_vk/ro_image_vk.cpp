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
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include <string_view>
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/async/worker.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/function_utils.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/device_memory_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/ktx_vulkan_adapter.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_fense_vk.hh"
#include "function/render_vk/ro_staging_buffer_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "glm/ext/vector_uint2.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <future>
#include <glm/glm.hpp>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_format_traits.hpp>

namespace WhiteBirdEngine {

namespace {

struct ImageReadbackSegment {
    VkDeviceSize buffer_offset = 0;
    size_t output_offset = 0;
    size_t size = 0;
};

struct ImageReadbackPlan {
    std::vector<VkBufferImageCopy> copy_regions;
    std::vector<ImageReadbackSegment> segments;
    VkDeviceSize buffer_size = 0;
    size_t output_size = 0;
    VkImageAspectFlags aspect = 0;
};

struct ImageReadbackOperation {
    std::future<Ref<ROFenseVK>> submission_future;
    // Set by copy_image_readback() once the GPU copy has completed.
    Ref<ROFenseVK> completion_fense;
    Ref<ROStagingBufferVK> staging_buffer;
    const void* mapped_memory = nullptr;
    size_t output_size = 0;
    std::vector<ImageReadbackSegment> segments;
};

VkDeviceSize align_copy_offset(VkDeviceSize p_offset) {
    constexpr VkDeviceSize COPY_OFFSET_ALIGNMENT = 4;
    if (p_offset > std::numeric_limits<VkDeviceSize>::max() - (COPY_OFFSET_ALIGNMENT - 1)) {
        throw std::runtime_error("Failed to acquire image content: aligned image byte size exceeds the CPU address space.");
    }
    return (p_offset + COPY_OFFSET_ALIGNMENT - 1) & ~(COPY_OFFSET_ALIGNMENT - 1);
}

uint64_t checked_multiply(uint64_t p_left, uint64_t p_right) {
    if (p_right != 0 && p_left > std::numeric_limits<uint64_t>::max() / p_right) {
        throw std::runtime_error("Failed to acquire image content: image byte size exceeds the CPU address space.");
    }
    return p_left * p_right;
}

size_t checked_image_level_size(
    VkExtent3D p_extent, const std::array<uint8_t, 3>& p_block_extent, uint8_t p_block_size, uint32_t p_layers_count) {
    const uint64_t block_count_x = (static_cast<uint64_t>(p_extent.width) + p_block_extent[0] - 1) / p_block_extent[0];
    const uint64_t block_count_y = (static_cast<uint64_t>(p_extent.height) + p_block_extent[1] - 1) / p_block_extent[1];
    const uint64_t block_count_z = (static_cast<uint64_t>(p_extent.depth) + p_block_extent[2] - 1) / p_block_extent[2];
    uint64_t byte_count = checked_multiply(block_count_x, block_count_y);
    byte_count = checked_multiply(byte_count, block_count_z);
    byte_count = checked_multiply(byte_count, p_block_size);
    byte_count = checked_multiply(byte_count, p_layers_count);
    if (byte_count > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("Failed to acquire image content: image byte size exceeds the CPU address space.");
    }
    return static_cast<size_t>(byte_count);
}

VkImageAspectFlags readback_aspect(VkFormat p_format) {
    const vk::Format format = static_cast<vk::Format>(p_format);
    if (vk::planeCount(format) != 1) {
        // A multiplanar image needs one independently described copy per plane.
        throw std::runtime_error("Failed to acquire image content: multiplanar formats are not supported.");
    }
    const bool has_depth = vk::hasDepthComponent(format);
    const bool has_stencil = vk::hasStencilComponent(format);
    if (has_depth && has_stencil) {
        // Combined depth-stencil packing cannot be represented by this single-aspect linear result.
        throw std::runtime_error("Failed to acquire image content: combined depth-stencil formats are not supported.");
    }
    if (has_depth) {
        // Copy only the depth aspect of a depth-only image.
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (has_stencil) {
        // Copy only the stencil aspect of a stencil-only image.
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if (!vk::isColor(format)) {
        // Reject formats that Vulkan does not expose as a copyable color, depth, or stencil aspect.
        throw std::runtime_error("Failed to acquire image content: the image format has no copyable aspect.");
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void validate_image_readback(const ROImageVK::ExposedFields& p_fields) {
    if ((p_fields.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        // Vulkan only permits image-to-buffer copies from transfer-source images.
        throw std::runtime_error("Failed to acquire image content: image was not created with transfer-source usage.");
    }
    if (p_fields.image_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        // An undefined layout explicitly indicates that the image contents cannot be preserved.
        throw std::runtime_error("Failed to acquire image content: image contents are undefined.");
    }
}

ImageReadbackPlan build_image_readback_plan(const ROImageVK::ExposedFields& p_fields) {
    ImageReadbackPlan plan;
    const vk::Format format = static_cast<vk::Format>(p_fields.format);
    const std::array<uint8_t, 3> block_extent = vk::blockExtent(format);
    const uint8_t block_size = vk::blockSize(format);
    if (block_size == 0 || block_extent[0] == 0 || block_extent[1] == 0 || block_extent[2] == 0) {
        // A zero format trait means the byte footprint cannot be calculated safely.
        throw std::runtime_error("Failed to acquire image content: image format size is unavailable.");
    }
    plan.aspect = readback_aspect(p_fields.format);
    plan.copy_regions.reserve(p_fields.mipmap_levels);
    plan.segments.reserve(p_fields.mipmap_levels);

    VkExtent3D level_extent = p_fields.extent;
    for (uint32_t level = 0; level < p_fields.mipmap_levels; ++level) {
        // Vulkan buffer-image copies require every mip's buffer offset to be four-byte aligned.
        plan.buffer_size = align_copy_offset(plan.buffer_size);
        const size_t level_size = checked_image_level_size(level_extent, block_extent, block_size, p_fields.layers_count);
        if (level_size > std::numeric_limits<VkDeviceSize>::max() - plan.buffer_size ||
            level_size > std::numeric_limits<size_t>::max() - plan.output_size) {
            // Refuse an image whose combined mip footprint cannot be represented by the GPU or CPU size types.
            throw std::runtime_error("Failed to acquire image content: combined mip data exceeds the CPU address space.");
        }
        plan.copy_regions.push_back(VkBufferImageCopy{.bufferOffset = plan.buffer_size,
            .imageSubresource = {.aspectMask = plan.aspect, .mipLevel = level, .baseArrayLayer = 0, .layerCount = p_fields.layers_count},
            .imageExtent = level_extent});
        plan.segments.push_back(
            ImageReadbackSegment{.buffer_offset = plan.buffer_size, .output_offset = plan.output_size, .size = level_size});
        plan.buffer_size += level_size;
        plan.output_size += level_size;
        level_extent.width = std::max(level_extent.width / 2, 1U);
        level_extent.height = std::max(level_extent.height / 2, 1U);
        level_extent.depth = std::max(level_extent.depth / 2, 1U);
    }
    return plan;
}

void record_image_readback(VkCommandBuffer p_command_buffer,
    VkImage p_image,
    VkImageLayout p_original_layout,
    VkImageSubresourceRange p_subresource_range,
    Ref<ROStagingBufferVK> p_staging_buffer,
    VkDeviceSize p_staging_offset,
    VkDeviceSize p_buffer_size,
    const std::vector<VkBufferImageCopy>& p_copy_regions) {
    // Move the image from its tracked layout into the engine's unified general layout for transfer.
    ROImageVK::MemoryBarrierBuilder()
        .set_image(p_image, p_original_layout)
        .set_src_access(VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
        .set_dst_access(VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT)
        .set_new_layout(VK_IMAGE_LAYOUT_GENERAL)
        .set_subresource_range(p_subresource_range)
        .apply_barrier(p_command_buffer);

    // Copy every mip and array layer into its planned staging-buffer region.
    vkCmdCopyImageToBuffer(p_command_buffer,
        p_image,
        VK_IMAGE_LAYOUT_GENERAL,
        p_staging_buffer->get_buffer(),
        static_cast<uint32_t>(p_copy_regions.size()),
        p_copy_regions.data());

    // Restore swapchain and other special images to the layout they had before acquisition.
    ROImageVK::MemoryBarrierBuilder()
        .set_image(p_image, VK_IMAGE_LAYOUT_GENERAL)
        .set_src_access(VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT)
        .set_dst_access(VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
        .set_new_layout(p_original_layout)
        .set_subresource_range(p_subresource_range)
        .apply_barrier(p_command_buffer);

    // Make transfer writes visible to the worker's coherent host-memory read.
    const VkBufferMemoryBarrier2 buffer_barrier =
        ROBufferVK::MemoryBarrierBuilder()
            .set_buffer(p_staging_buffer->get_buffer(), p_staging_buffer->fields().buffer_size)
            .set_src_access(VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT)
            .set_dst_access(VK_ACCESS_2_HOST_READ_BIT, VK_PIPELINE_STAGE_2_HOST_BIT)
            .set_data_region(p_staging_offset, p_buffer_size)
            .build();
    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.bufferMemoryBarrierCount = 1;
    dependency_info.pBufferMemoryBarriers = &buffer_barrier;
    vkCmdPipelineBarrier2(p_command_buffer, &dependency_info);
}

ImageReadbackOperation begin_image_readback(
    const ROImageVK::ExposedFields& p_fields, const RenderContextVK* p_render_data, ImageReadbackPlan p_plan) {
    RendererVK* renderer = RendererVK::get_singleton();
    if (renderer == nullptr) {
        throw std::runtime_error("Failed to acquire image content: the Vulkan renderer is unavailable.");
    }

    // Allocate a dedicated mapped staging buffer that remains alive through the worker copy.
    const size_t staging_buffer_size = ROStagingBufferVK::required_buffer_size(static_cast<size_t>(p_plan.buffer_size));
    Ref<ROStagingBufferVK> staging_buffer =
        p_render_data->create_staging_buffer(ROStagingBufferVK::Builder().set_size(staging_buffer_size).set_usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    const ROStagingBufferVK::Allocation staging_allocation = staging_buffer->allocate_region(static_cast<size_t>(p_plan.buffer_size));
    for (VkBufferImageCopy& copy_region : p_plan.copy_regions) {
        // Translate plan-relative offsets into the staging buffer's suballocation.
        copy_region.bufferOffset += staging_allocation.offset;
    }

    // Preserve the current layout so the GPU job can restore it after the transfer.
    const VkImageSubresourceRange subresource_range{.aspectMask = p_plan.aspect,
        .baseMipLevel = 0,
        .levelCount = p_fields.mipmap_levels,
        .baseArrayLayer = 0,
        .layerCount = p_fields.layers_count};

    // Register the GPU copy and receive its completion fence only after queue submission succeeds.
    std::future<Ref<ROFenseVK>> submission_future =
        renderer->register_gpu_job([image = p_fields.image,
                                       image_layout = p_fields.image_layout,
                                       subresource_range,
                                       staging_buffer,
                                       staging_offset = staging_allocation.offset,
                                       buffer_size = p_plan.buffer_size,
                                       copy_regions = std::move(p_plan.copy_regions)](VkCommandBuffer p_command_buffer) {
            record_image_readback(
                p_command_buffer, image, image_layout, subresource_range, staging_buffer, staging_offset, buffer_size, copy_regions);
        });

    return ImageReadbackOperation{.submission_future = std::move(submission_future),
        .staging_buffer = std::move(staging_buffer),
        .mapped_memory = staging_allocation.data,
        .output_size = p_plan.output_size,
        .segments = std::move(p_plan.segments)};
}

void copy_image_readback(ImageReadbackOperation& p_operation, std::span<uint8_t> p_destination) {
    // Wait until queue submission has finished before accessing its externally synchronized Vulkan fence.
    p_operation.completion_fense = p_operation.submission_future.get();
    p_operation.completion_fense->wait();

    // Remove Vulkan's per-mip alignment gaps while assembling the tightly packed CPU result.
    for (const ImageReadbackSegment& segment : p_operation.segments) {
        std::memcpy(p_destination.data() + segment.output_offset,
            static_cast<const uint8_t*>(p_operation.mapped_memory) + segment.buffer_offset,
            segment.size);
    }

}

// Transfer final render-object ownership back to the rendering thread for delayed destruction. The references are moved,
// not copied, so the worker never ends up holding the final one. A renderer that has stopped accepting jobs drops the job,
// and with it the references, here.
void release_image_readback(ImageReadbackOperation& p_operation) {
    RendererVK* renderer = RendererVK::get_singleton();
    WBE_DEBUG_ASSERT(renderer != nullptr);
    static_cast<void>(renderer->try_add_job(
        [completion_fense = std::move(p_operation.completion_fense), staging_buffer = std::move(p_operation.staging_buffer)]() {
            static_cast<void>(completion_fense);
            static_cast<void>(staging_buffer);
        }));
}

// The returned future is ready once the bytes are copied, before the staging release is queued on the rendering thread. A
// rendering thread waiting on it therefore never waits for room in its own full job buffer.
template <typename TCompletion>
auto schedule_image_readback(
    const ROImageVK::ExposedFields& p_fields, const RenderContextVK* p_render_data, size_t p_destination_size, TCompletion p_completion) {
    Worker* worker = Worker::get_singleton();
    if (worker == nullptr) {
        throw std::runtime_error("Failed to acquire image content: the engine worker is unavailable.");
    }

    // Validate and size the complete readback before registering GPU work.
    validate_image_readback(p_fields);
    ImageReadbackPlan plan = build_image_readback_plan(p_fields);
    if (p_destination_size < plan.output_size) {
        throw std::runtime_error(std::format(
            "Failed to acquire image content: destination has {} bytes but {} bytes are required.", p_destination_size, plan.output_size));
    }
    ImageReadbackOperation operation = begin_image_readback(p_fields, p_render_data, std::move(plan));

    // Use one worker task for the fence wait, CPU copy, and overload-specific result assembly. The packaged task stores the
    // copy's result or exception in the returned future.
    using Result = std::invoke_result_t<TCompletion&, ImageReadbackOperation&>;
    std::packaged_task<Result(ImageReadbackOperation&)> copy_task(std::move(p_completion));
    std::future<Result> result = copy_task.get_future();
    // Failures after the copy have no future to report to, so they terminate the program.
    static_cast<void>(worker->submit_task([operation = std::move(operation), copy_task = std::move(copy_task)]() mutable noexcept {
        copy_task(operation);
        release_image_readback(operation);
    }));
    return result;
}

} // namespace

Ref<ROImageVK> ImageDescVK::create_ref(const RenderContextVK* p_render_data) const {
    return p_render_data->create_image(*this);
}

Unique<ROImageVK> ImageDescVK::create_unique(const RenderContextVK* p_render_data) const {
    return make_unique<ROImageVK>(p_render_data->allocator, p_render_data, *this);
}

Ref<ROImageVK> ImageAliasDescVK::create_ref(const RenderContextVK* p_render_data) const {
    return p_render_data->create_image(*this);
}

Unique<ROImageVK> ImageAliasDescVK::create_unique(const RenderContextVK* p_render_data) const {
    return make_unique<ROImageVK>(p_render_data->allocator, p_render_data, *this);
}

TypeID ROImageVK::get_type_uuid() const {
    return TypeUUID<ROImageVK>::UUID;
}

ImageDescVK& ImageDescVK::set_image_type(VkImageType p_image_type) {
    image_type = p_image_type;
    return *this;
}

ImageDescVK& ImageDescVK::set_format(VkFormat p_format) {
    format = p_format;
    return *this;
}

ImageDescVK& ImageDescVK::set_extent(VkExtent3D p_extent) {
    extent = p_extent;
    return *this;
}

ImageDescVK& ImageDescVK::set_mipmap_level(uint32_t p_mipmap_level) {
    mipmap_level = p_mipmap_level;
    return *this;
}

ImageDescVK& ImageDescVK::set_layers(uint32_t p_layers_count) {
    layers_count = p_layers_count;
    return *this;
}

ImageDescVK& ImageDescVK::set_usage(VkImageUsageFlags p_usage) {
    usage = p_usage;
    return *this;
}

ImageDescVK& ImageDescVK::set_queue_families(const std::set<uint32_t>& p_queue_families) {
    queue_families = p_queue_families;
    return *this;
}

ImageDescVK& ImageDescVK::set_properties_2d(VkFormat p_format, VkExtent2D p_extent, VkImageUsageFlags p_usage) {
    image_type = VK_IMAGE_TYPE_2D;
    format = p_format;
    extent = VkExtent3D{.width = p_extent.width, .height = p_extent.height, .depth = 1};
    usage = p_usage;
    return *this;
}

ImageDescVK& ImageDescVK::image_2d(glm::uvec2 p_size) {
    image_type = VK_IMAGE_TYPE_2D;
    extent = VkExtent3D{.width = p_size.x, .height = p_size.y, .depth = 1};
    return *this;
}

ImageDescVK& ImageDescVK::image_2d(VkExtent2D p_extent) {
    image_type = VK_IMAGE_TYPE_2D;
    extent = VkExtent3D{.width = p_extent.width, .height = p_extent.height, .depth = 1};
    return *this;
}

ImageDescVK& ImageDescVK::image_3d(glm::uvec3 p_size) {
    image_type = VK_IMAGE_TYPE_3D;
    extent = VkExtent3D{.width = p_size.x, .height = p_size.y, .depth = p_size.z};
    return *this;
}

ImageDescVK& ImageDescVK::image_3d(VkExtent3D p_extent) {
    image_type = VK_IMAGE_TYPE_3D;
    extent = p_extent;
    return *this;
}

ImageDescVK& ImageDescVK::rgb() {
    format = VK_FORMAT_R8G8B8_UNORM;
    return *this;
}

ImageDescVK& ImageDescVK::srgb() {
    format = VK_FORMAT_R8G8B8_SRGB;
    return *this;
}

ImageDescVK& ImageDescVK::rgba() {
    format = VK_FORMAT_R8G8B8A8_UNORM;
    return *this;
}

ImageDescVK& ImageDescVK::srgba() {
    format = VK_FORMAT_R8G8B8A8_SRGB;
    return *this;
}

ImageDescVK& ImageDescVK::rgb_hdr() {
    format = VK_FORMAT_R32G32B32_SFLOAT;
    return *this;
}

ImageDescVK& ImageDescVK::rgba_hdr() {
    format = VK_FORMAT_R32G32B32A32_SFLOAT;
    return *this;
}

void ROImageVK::MemoryBarrierBuilder::apply_barrier(VkCommandBuffer p_command_buffer) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage_mask;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstStageMask = dst_stage_mask;
    barrier.dstAccessMask = dst_access_mask;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    WBE_DEBUG_ASSERT(image_handle != VK_NULL_HANDLE);
    barrier.image = image_handle;
    barrier.subresourceRange = subresource_range;
    VkDependencyInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    info.imageMemoryBarrierCount = 1;
    info.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(p_command_buffer, &info);
    // Keep later graph barriers aligned with the layout this command buffer will establish.
    if (image != MEM_NULL) {
        image->exposed_fields.image_layout = new_layout;
    }
}

ROImageVK::ROImageVK(const RenderContextVK* p_render_data, const Builder& p_builder) : IRenderObjectVK(p_render_data) {
    create_image(p_builder, MEM_NULL, 0);
    memory = DeviceMemoryVK::Builder().set_image_requirements(exposed_fields.image).set_domain(DeviceDomain::DEVICE).create_ref(render_data);
    VkResult result = vmaBindImageMemory(render_data->vma_allocator, memory->get(), exposed_fields.image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to bind Vulkan image memory: {}.", string_VkResult(result)));
    }
    exposed_fields.image_memory = memory->fields().device_memory;
}

ROImageVK::ROImageVK(const RenderContextVK* p_render_data, const AliasBuilder& p_builder) : IRenderObjectVK(p_render_data) {
    if (p_builder.memory == MEM_NULL) {
        throw std::runtime_error("Failed to create aliasing image: device memory is null.");
    }
    create_image(p_builder.image, p_builder.memory, p_builder.offset);
    memory = p_builder.memory;
    exposed_fields.image_memory = memory->fields().device_memory;
}

ROImageVK::ROImageVK(const RenderContextVK* p_render_data, VkImage p_image, VkImageLayout p_initial_image_layout)
    : IRenderObjectVK(p_render_data) {
    exposed_fields.image = p_image;
    p_image = VK_NULL_HANDLE;
    exposed_fields.image_memory = VK_NULL_HANDLE;
    exposed_fields.image_layout = p_initial_image_layout;
    exposed_fields.usage = 0;
    exposed_fields.is_swap_chain_image = true;
    ownership = Ownership::SWAP_CHAIN;
}

ROImageVK::ROImageVK(const RenderContextVK* p_render_data, ktxVulkanTexture p_texture) : IRenderObjectVK(p_render_data) {
    if (p_texture.image == VK_NULL_HANDLE || p_texture.deviceMemory == VK_NULL_HANDLE) {
        ktxVulkanTexture_Destruct(&p_texture, p_render_data->logic_device, p_render_data->allocation_callbacks);
        throw std::runtime_error("Failed to adopt libktx Vulkan texture: image or device memory is null.");
    }
    ktx_texture = p_texture;
    exposed_fields.image = p_texture.image;
    exposed_fields.image_memory = p_texture.deviceMemory;
    exposed_fields.image_layout = p_texture.imageLayout;
    exposed_fields.format = p_texture.imageFormat;
    exposed_fields.extent = VkExtent3D{.width = p_texture.width, .height = p_texture.height, .depth = p_texture.depth};
    exposed_fields.view_type = p_texture.viewType;
    exposed_fields.mipmap_levels = p_texture.levelCount;
    exposed_fields.layers_count = p_texture.layerCount;
    exposed_fields.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ownership = Ownership::KTX;
}

ROImageVK::~ROImageVK() {
    // Deallocate resource after frames in flight.
    if (ownership == Ownership::KTX && ktx_texture.image != VK_NULL_HANDLE) {
        render_data->register_delayed_call(
            [render_data = render_data, texture = ktx_texture]() mutable {
                ktxVulkanTexture_Destruct(&texture, render_data->logic_device, render_data->allocation_callbacks);
            },
            render_data->frames_in_flight);
    } else if (ownership == Ownership::DEVICE_MEMORY && exposed_fields.image != VK_NULL_HANDLE) {
        // The memory reference is released after the image is destroyed.
        render_data->register_delayed_call(
            [render_data = render_data, image = exposed_fields.image, memory = memory]() mutable {
                vkDestroyImage(render_data->logic_device, image, render_data->allocation_callbacks);
                memory = MEM_NULL;
            },
            render_data->frames_in_flight);
    }
}

void ROImageVK::create_image(const Builder& p_builder, Ref<DeviceMemoryVK> p_alias_memory, VkDeviceSize p_alias_offset) {
    exposed_fields.queue_families = p_builder.queue_families;
    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = WBE_REQUIRED(p_builder.image_type);
    create_info.format = WBE_REQUIRED(p_builder.format);
    create_info.extent = WBE_REQUIRED(p_builder.extent);
    create_info.mipLevels = p_builder.mipmap_level;
    create_info.arrayLayers = p_builder.layers_count;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = WBE_REQUIRED(p_builder.usage) | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    Vector<uint32_t, RenderAllocator> queue_families(render_data->allocator);
    if (p_builder.queue_families.empty()) {
        queue_families = Vector<uint32_t, RenderAllocator>(
            render_data->queue_family_indices.begin(), render_data->queue_family_indices.end(), render_data->allocator);
    } else {
        queue_families = Vector<uint32_t, RenderAllocator>(
            p_builder.queue_families.begin(), p_builder.queue_families.end(), render_data->allocator);
    }
    if (queue_families.size() > 1) {
        create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.pQueueFamilyIndices = queue_families.data();
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_families.size());
    } else {
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (p_alias_memory != MEM_NULL) {
        exposed_fields.image = p_alias_memory->create_image(create_info, p_alias_offset);
    } else {
        VkResult result =
            vkCreateImage(render_data->logic_device, &create_info, render_data->allocation_callbacks, &exposed_fields.image);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::format("Failed to create Vulkan image: {}.", string_VkResult(result)));
        }
    }
    exposed_fields.format = WBE_REQUIRED(p_builder.format);
    exposed_fields.extent = WBE_REQUIRED(p_builder.extent);
    switch (WBE_REQUIRED(p_builder.image_type)) {
    case VK_IMAGE_TYPE_1D:
        exposed_fields.view_type = p_builder.layers_count > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        break;
    case VK_IMAGE_TYPE_2D:
        exposed_fields.view_type = p_builder.layers_count > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case VK_IMAGE_TYPE_3D:
        exposed_fields.view_type = VK_IMAGE_VIEW_TYPE_3D;
        break;
    default:
        throw std::runtime_error("Failed to determine default image view type: unsupported image type.");
    }
    exposed_fields.mipmap_levels = p_builder.mipmap_level;
    exposed_fields.layers_count = p_builder.layers_count;
    exposed_fields.usage = create_info.usage;
}

void ROImageVK::set_image_data_immediate(VkCommandBuffer p_command_buffer, Ref<ROBufferVK> p_data, VkBufferImageCopy p_region_info) {
    VkImageSubresourceRange subresource_range{.aspectMask = p_region_info.imageSubresource.aspectMask,
        .baseMipLevel = p_region_info.imageSubresource.mipLevel,
        .levelCount = 1,
        .baseArrayLayer = p_region_info.imageSubresource.baseArrayLayer,
        .layerCount = p_region_info.imageSubresource.layerCount};
    auto apply_upload_barrier = [this, p_command_buffer, subresource_range](VkAccessFlags2 p_src_access, VkAccessFlags2 p_dst_access) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = p_src_access;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = p_dst_access;
        barrier.oldLayout = exposed_fields.image_layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = exposed_fields.image;
        barrier.subresourceRange = subresource_range;
        VkDependencyInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.imageMemoryBarrierCount = 1;
        info.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(p_command_buffer, &info);
        exposed_fields.image_layout = VK_IMAGE_LAYOUT_GENERAL;
    };
    apply_upload_barrier(VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdCopyBufferToImage(p_command_buffer, p_data->get_buffer(), exposed_fields.image, VK_IMAGE_LAYOUT_GENERAL, 1, &p_region_info);
    // Make the uploaded texels visible to shader sampling before the image is used by a draw.
    apply_upload_barrier(VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

std::future<std::vector<uint8_t>> ROImageVK::acquire_image_content() const {
    return schedule_image_readback(exposed_fields, render_data, std::numeric_limits<size_t>::max(), [](ImageReadbackOperation& p_operation) {
        std::vector<uint8_t> content(p_operation.output_size);
        copy_image_readback(p_operation, content);
        return content;
    });
}

std::future<void> ROImageVK::acquire_image_content(std::span<uint8_t> p_destination) const {
    return schedule_image_readback(
        exposed_fields, render_data, p_destination.size_bytes(), [p_destination](ImageReadbackOperation& p_operation) {
            copy_image_readback(p_operation, p_destination);
        });
}

size_t ROImageVK::get_content_size() const {
    validate_image_readback(exposed_fields);
    return build_image_readback_plan(exposed_fields).output_size;
}

Ref<ROImageViewVK> ROImageVK::get_image_view_ref() const {
    return get_image_view_ref(exposed_fields.view_type,
        exposed_fields.format,
        VkImageSubresourceRange{.aspectMask = 0,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS},
        VkComponentMapping{});
}

Ref<ROImageViewVK> ROImageVK::get_image_view_ref(VkImageViewType p_view_type,
    VkFormat p_format,
    VkImageSubresourceRange p_subresource_range,
    VkComponentMapping p_components) const {
    if (p_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("Failed to create image view: image view format is unavailable.");
    }
    if (p_subresource_range.baseMipLevel >= exposed_fields.mipmap_levels ||
        p_subresource_range.baseArrayLayer >= exposed_fields.layers_count) {
        throw std::runtime_error("Failed to create image view: subresource range starts outside the image.");
    }
    const uint32_t remaining_levels = exposed_fields.mipmap_levels - p_subresource_range.baseMipLevel;
    const uint32_t remaining_layers = exposed_fields.layers_count - p_subresource_range.baseArrayLayer;
    if (p_subresource_range.levelCount == VK_REMAINING_MIP_LEVELS) {
        p_subresource_range.levelCount = remaining_levels;
    }
    if (p_subresource_range.layerCount == VK_REMAINING_ARRAY_LAYERS) {
        p_subresource_range.layerCount = remaining_layers;
    }
    if (p_subresource_range.levelCount == 0 || p_subresource_range.levelCount > remaining_levels ||
        p_subresource_range.layerCount == 0 || p_subresource_range.layerCount > remaining_layers) {
        throw std::runtime_error("Failed to create image view: subresource range exceeds the image.");
    }
    if (p_subresource_range.aspectMask == 0) {
        const auto format = static_cast<vk::Format>(exposed_fields.format);
        if (vk::hasDepthComponent(format)) {
            p_subresource_range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if (vk::hasStencilComponent(format)) {
            p_subresource_range.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        if (p_subresource_range.aspectMask == 0) {
            p_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }
    const ImageViewDescVK description = ImageViewDescVK{}
                                            .set_image(acquire_this_ref().dynamic_cast_ref<ROImageVK>())
                                            .set_view_type(p_view_type)
                                            .set_format(p_format)
                                            .set_components(p_components)
                                            .set_subresource_range(p_subresource_range);
    return render_data->create_image_view(description);
}

void ROImageVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_render_object(p_graph, p_resource_id, acquire_this_ref());
}

void ROImageVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_IMAGE, get_image(), p_name);
}

} // namespace WhiteBirdEngine

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
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/async/worker.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/function_utils.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/device_memory_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/render_vk/ro_fense_vk.hh"
#include "function/render_vk/ro_staging_buffer_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <cstring>
#include <format>
#include <future>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

struct BufferReadbackOperation {
    std::future<Ref<ROFenseVK>> submission_future;
    // Set by copy_device_buffer_readback() once the GPU copy has completed.
    Ref<ROFenseVK> completion_fense;
    Ref<ROStagingBufferVK> staging_buffer;
    const void* mapped_memory = nullptr;
    size_t size = 0;
};

void validate_buffer_readback_range(size_t p_buffer_size, size_t p_source_offset, size_t p_size) {
    if (p_size == 0) {
        throw std::runtime_error("Failed to acquire buffer content: byte count is zero.");
    }
    if (p_source_offset > p_buffer_size || p_size > p_buffer_size - p_source_offset) {
        throw std::runtime_error(std::format(
            "Failed to acquire buffer content: {} bytes at offset {} exceed buffer size {}.", p_size, p_source_offset, p_buffer_size));
    }
}

void record_buffer_readback(VkCommandBuffer p_command_buffer,
    VkBuffer p_source_buffer,
    size_t p_source_offset,
    Ref<ROStagingBufferVK> p_staging_buffer,
    VkDeviceSize p_staging_offset,
    size_t p_size) {
    const VkBufferMemoryBarrier2 source_barrier =
        ROBufferVK::MemoryBarrierBuilder()
            .set_buffer(p_source_buffer, p_source_offset + p_size)
            .set_src_access(VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
            .set_dst_access(VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT)
            .set_data_region(p_source_offset, p_size)
            .build();
    VkDependencyInfo source_dependency{};
    source_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    source_dependency.bufferMemoryBarrierCount = 1;
    source_dependency.pBufferMemoryBarriers = &source_barrier;
    vkCmdPipelineBarrier2(p_command_buffer, &source_dependency);

    const VkBufferCopy copy_region{
        .srcOffset = p_source_offset,
        .dstOffset = p_staging_offset,
        .size = p_size,
    };
    vkCmdCopyBuffer(p_command_buffer, p_source_buffer, p_staging_buffer->get_buffer(), 1, &copy_region);

    const VkBufferMemoryBarrier2 destination_barrier =
        ROBufferVK::MemoryBarrierBuilder()
            .set_buffer(p_staging_buffer->get_buffer(), p_staging_buffer->fields().buffer_size)
            .set_src_access(VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT)
            .set_dst_access(VK_ACCESS_2_HOST_READ_BIT, VK_PIPELINE_STAGE_2_HOST_BIT)
            .set_data_region(p_staging_offset, p_size)
            .build();
    VkDependencyInfo destination_dependency{};
    destination_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    destination_dependency.bufferMemoryBarrierCount = 1;
    destination_dependency.pBufferMemoryBarriers = &destination_barrier;
    vkCmdPipelineBarrier2(p_command_buffer, &destination_dependency);
}

BufferReadbackOperation begin_device_buffer_readback(
    const RenderContextVK* p_render_data, VkBuffer p_source_buffer, size_t p_source_offset, size_t p_size) {
    RendererVK* renderer = RendererVK::get_singleton();
    if (renderer == nullptr) {
        throw std::runtime_error("Failed to acquire buffer content: the Vulkan renderer is unavailable.");
    }
    const size_t staging_buffer_size = ROStagingBufferVK::required_buffer_size(p_size);
    Ref<ROStagingBufferVK> staging_buffer =
        p_render_data->create_staging_buffer(ROStagingBufferVK::Builder().set_size(staging_buffer_size).set_usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    const ROStagingBufferVK::Allocation staging_allocation = staging_buffer->allocate_region(p_size);
    std::future<Ref<ROFenseVK>> submission_future = renderer->register_gpu_job([source_buffer = p_source_buffer,
                                                                                   source_offset = p_source_offset,
                                                                                   staging_buffer,
                                                                                   staging_offset = staging_allocation.offset,
                                                                                   size = p_size](VkCommandBuffer p_command_buffer) {
        record_buffer_readback(p_command_buffer, source_buffer, source_offset, staging_buffer, staging_offset, size);
    });
    return BufferReadbackOperation{.submission_future = std::move(submission_future),
        .staging_buffer = std::move(staging_buffer),
        .mapped_memory = staging_allocation.data,
        .size = p_size};
}

void copy_device_buffer_readback(BufferReadbackOperation& p_operation, std::span<uint8_t> p_destination) {
    p_operation.completion_fense = p_operation.submission_future.get();
    p_operation.completion_fense->wait();
    std::memcpy(p_destination.data(), p_operation.mapped_memory, p_operation.size);
}

// Staging buffers and fences are render objects, so hand their last references to the rendering thread. They are moved,
// not copied, so the worker never ends up holding the final reference. A renderer that has stopped accepting jobs drops
// the job, and with it the references, here.
void release_device_buffer_readback(BufferReadbackOperation& p_operation) {
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
auto schedule_device_buffer_readback(const RenderContextVK* p_render_data,
    VkBuffer p_source_buffer,
    size_t p_buffer_size,
    VkBufferUsageFlags p_usage,
    size_t p_source_offset,
    size_t p_size,
    TCompletion p_completion) {
    Worker* worker = Worker::get_singleton();
    if (worker == nullptr) {
        throw std::runtime_error("Failed to acquire buffer content: the engine worker is unavailable.");
    }
    validate_buffer_readback_range(p_buffer_size, p_source_offset, p_size);
    if ((p_usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0) {
        throw std::runtime_error("Failed to acquire buffer content: buffer was not created with transfer-source usage.");
    }
    BufferReadbackOperation operation = begin_device_buffer_readback(p_render_data, p_source_buffer, p_source_offset, p_size);
    using Result = std::invoke_result_t<TCompletion&, BufferReadbackOperation&>;
    // The packaged task stores the copy's result or exception in the returned future.
    std::packaged_task<Result(BufferReadbackOperation&)> copy_task(std::move(p_completion));
    std::future<Result> result = copy_task.get_future();
    // Failures after the copy have no future to report to, so they terminate the program.
    static_cast<void>(worker->submit_task([operation = std::move(operation), copy_task = std::move(copy_task)]() mutable noexcept {
        copy_task(operation);
        release_device_buffer_readback(operation);
    }));
    return result;
}

} // namespace

BufferDescVK& BufferDescVK::set_properties(
    size_t p_element_size, size_t p_element_count, VkBufferUsageFlags p_usage, DeviceDomain p_domain) {
    if (p_element_size == 0 || p_element_count == 0) {
        throw std::runtime_error("Failed to set buffer properties: element size and count must be non-zero.");
    }
    if (p_element_count > std::numeric_limits<size_t>::max() / p_element_size) {
        throw std::runtime_error("Failed to set buffer properties: buffer size overflows size_t.");
    }
    size = p_element_size * p_element_count;
    usage = p_usage;
    domain = p_domain;
    return *this;
}

BufferDescVK& BufferDescVK::set_properties(size_t p_element_size, VkBufferUsageFlags p_usage, DeviceDomain p_domain) {
    return set_properties(p_element_size, 1, p_usage, p_domain);
}

BufferDescVK& BufferDescVK::initial_data(std::span<const uint8_t> p_data, DeviceDomain p_domain) {
    if (p_data.empty()) {
        throw std::runtime_error("Failed to configure initial buffer data: data is empty.");
    }
    size = p_data.size();
    domain = p_domain;
    initial_data_bytes.assign(p_data.begin(), p_data.end());
    if (p_domain == DeviceDomain::DEVICE) {
        add_usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    return *this;
}

BufferDescVK& BufferDescVK::initial_data(const void* p_data, size_t p_size, DeviceDomain p_domain) {
    if (p_data == nullptr) {
        throw std::runtime_error("Failed to configure initial buffer data: data is null.");
    }
    return initial_data(std::span<const uint8_t>(static_cast<const uint8_t*>(p_data), p_size), p_domain);
}

namespace {

DebugData make_descriptor_row(
    std::string_view p_resource_type, std::string_view p_descriptor_type, uint32_t p_heap_index, VkDeviceAddress p_device_address) {
    DebugData row;
    row.set_value("resource_type", std::string(p_resource_type));
    row.set_value("descriptor_type", std::string(p_descriptor_type));
    row.set_value("heap", "resource");
    row.set_value("heap_index", p_heap_index);
    row.set_value("device_address", std::format("0x{:x}", p_device_address));
    return row;
}

} // namespace

Ref<ROBufferVK> BufferDescVK::create_ref(const RenderContextVK* p_render_data) const {
    return p_render_data->create_buffer(*this);
}

Unique<ROBufferVK> BufferDescVK::create_unique(const RenderContextVK* p_render_data) const {
    return make_unique<ROBufferVK>(p_render_data->allocator, p_render_data, *this);
}

Ref<ROBufferVK> BufferAliasDescVK::create_ref(const RenderContextVK* p_render_data) const {
    return p_render_data->create_buffer(*this);
}

Unique<ROBufferVK> BufferAliasDescVK::create_unique(const RenderContextVK* p_render_data) const {
    return make_unique<ROBufferVK>(p_render_data->allocator, p_render_data, *this);
}

TypeID ROBufferVK::get_type_uuid() const {
    return TypeUUID<ROBufferVK>::UUID;
}

namespace {

VkDeviceAddressRangeEXT get_buffer_address_range(const RenderContextVK* p_render_data, VkBuffer p_buffer, VkDeviceSize p_size) {
    VkBufferDeviceAddressInfo address_info{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = p_buffer};
    return VkDeviceAddressRangeEXT{.address = vkGetBufferDeviceAddress(p_render_data->logic_device, &address_info), .size = p_size};
}

void write_buffer_descriptor(
    const RenderContextVK* p_render_data, uint32_t p_index, VkDescriptorType p_type, VkBuffer p_buffer, VkDeviceSize p_size) {
    VkDeviceAddressRangeEXT address_range = get_buffer_address_range(p_render_data, p_buffer, p_size);
    VkResourceDescriptorInfoEXT descriptor{};
    descriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    descriptor.type = p_type;
    descriptor.data.pAddressRange = &address_range;
    const DescriptorArenaLocationVK arena = p_render_data->buffer_descriptor_arena;
    p_render_data->descriptor_heap_allocator->write_resource_descriptor(arena.heap_id, arena.arena_id, p_index, descriptor);
}

VkBufferUsageFlags get_buffer_usage(const BufferDescVK& p_builder, DeviceDomain p_domain) {
    VkBufferUsageFlags usage = WBE_REQUIRED(p_builder.usage);
    if (!p_builder.initial_data_bytes.empty() && p_domain == DeviceDomain::DEVICE) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if ((usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) != 0) {
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    return usage;
}

std::vector<uint32_t> get_initial_queue_families(const RenderContextVK* p_render_data, const BufferDescVK& p_builder) {
    if (p_builder.queue_family_indices.empty()) {
        return {p_render_data->queue_family_indices.begin(), p_render_data->queue_family_indices.end()};
    }
    return {p_builder.queue_family_indices.begin(), p_builder.queue_family_indices.end()};
}

void validate_distinct_descriptor_indices(const BufferDescVK& p_builder) {
    if (p_builder.uniform_view_index.has_value() && p_builder.storage_view_index.has_value() &&
        p_builder.uniform_view_index == p_builder.storage_view_index) {
        throw std::runtime_error("Failed to create buffer: uniform and storage descriptors cannot adopt the same heap index.");
    }
}

void validate_adopted_descriptor_indices(
    const RenderContextVK* p_render_data, const BufferDescVK& p_builder, VkBufferUsageFlags p_usage) {
    if (p_builder.uniform_view_index.has_value()) {
        if ((p_usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == 0) {
            throw std::runtime_error("Failed to adopt uniform-buffer descriptor index: buffer does not have uniform-buffer usage.");
        }
        if (p_render_data->descriptor_heap_allocator == nullptr ||
            !p_render_data->descriptor_heap_allocator->owns_descriptor(p_render_data->buffer_descriptor_arena.heap_id,
                p_render_data->buffer_descriptor_arena.arena_id,
                *p_builder.uniform_view_index)) {
            throw std::runtime_error("Failed to adopt uniform-buffer descriptor index: the index is not allocated by the buffer arena.");
        }
    }
    if (p_builder.storage_view_index.has_value()) {
        if ((p_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0) {
            throw std::runtime_error("Failed to adopt storage-buffer descriptor index: buffer does not have storage-buffer usage.");
        }
        if (p_render_data->descriptor_heap_allocator == nullptr ||
            !p_render_data->descriptor_heap_allocator->owns_descriptor(p_render_data->buffer_descriptor_arena.heap_id,
                p_render_data->buffer_descriptor_arena.arena_id,
                *p_builder.storage_view_index)) {
            throw std::runtime_error("Failed to adopt storage-buffer descriptor index: the index is not allocated by the buffer arena.");
        }
    }
}

VkBufferCreateInfo make_buffer_create_info(VkDeviceSize p_size, VkBufferUsageFlags p_usage, const std::vector<uint32_t>& p_initial_queues) {
    VkBufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = p_size;
    create_info.usage = p_usage;
    if (p_initial_queues.size() > 1) {
        create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.pQueueFamilyIndices = p_initial_queues.data();
        create_info.queueFamilyIndexCount = static_cast<uint32_t>(p_initial_queues.size());
    } else {
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    return create_info;
}

void adopt_buffer_descriptors(const RenderContextVK* p_render_data,
    const BufferDescVK& p_builder,
    VkBuffer p_buffer,
    VkDeviceSize p_buffer_size,
    std::optional<uint32_t>& p_uniform_view_index,
    std::optional<uint32_t>& p_storage_view_index) {
    if (p_builder.uniform_view_index.has_value()) {
        p_uniform_view_index = p_builder.uniform_view_index;
        write_buffer_descriptor(p_render_data, *p_uniform_view_index, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, p_buffer, p_buffer_size);
    }
    if (p_builder.storage_view_index.has_value()) {
        p_storage_view_index = p_builder.storage_view_index;
        write_buffer_descriptor(p_render_data, *p_storage_view_index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, p_buffer, p_buffer_size);
    }
}

void upload_initial_data(
    ROBufferVK* p_buffer, const RenderContextVK* p_render_data, const BufferDescVK& p_builder, DeviceDomain p_domain) {
    if (p_builder.initial_data_bytes.empty()) {
        return;
    }
    if (p_domain == DeviceDomain::DEVICE) {
        Ref<ROStagingBufferVK> staging_buffer =
            p_render_data->create_staging_buffer(ROStagingBufferVK::Builder()
                .set_size(ROStagingBufferVK::required_buffer_size(p_builder.initial_data_bytes.size()))
                .set_usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                );
        void* staging_data = staging_buffer->allocate(p_builder.initial_data_bytes.size());
        std::memcpy(staging_data, p_builder.initial_data_bytes.data(), p_builder.initial_data_bytes.size());
        p_buffer->update_buffer(staging_buffer, staging_data, p_builder.initial_data_bytes.size());
        return;
    }
    p_buffer->update_buffer(p_builder.initial_data_bytes.data(), p_builder.initial_data_bytes.size());
}

} // namespace

ROBufferVK::ROBufferVK(const RenderContextVK* p_render_data, const Builder& p_builder) : IRenderObjectVK(p_render_data) {
    exposed_fields.domain = WBE_REQUIRED(p_builder.domain);
    create_buffer(p_builder, false);
    memory = DeviceMemoryVK::Builder().set_buffer_requirements(buffer).set_domain(exposed_fields.domain).create_ref(render_data);
    VkResult result = vmaBindBufferMemory(render_data->vma_allocator, memory->get(), buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to bind Vulkan buffer memory: {}.", string_VkResult(result)));
    }
    finish_creation(p_builder);
}

ROBufferVK::ROBufferVK(const RenderContextVK* p_render_data, const AliasBuilder& p_builder)
    : IRenderObjectVK(p_render_data) {
    if (p_builder.memory == MEM_NULL) {
        throw std::runtime_error("Failed to create aliasing buffer: device memory is null.");
    }
    exposed_fields.domain = p_builder.memory->fields().domain;
    if (p_builder.buffer.domain.has_value() && *p_builder.buffer.domain != exposed_fields.domain) {
        throw std::runtime_error("Failed to create aliasing buffer: the buffer domain differs from the device memory domain.");
    }
    memory = p_builder.memory;
    memory_offset = p_builder.offset;
    create_buffer(p_builder.buffer, true);
    finish_creation(p_builder.buffer);
}

void ROBufferVK::create_buffer(const Builder& p_builder, bool p_alias) {
    validate_distinct_descriptor_indices(p_builder);
    usage = get_buffer_usage(p_builder, exposed_fields.domain);
    validate_adopted_descriptor_indices(render_data, p_builder, usage);
    std::vector<uint32_t> initial_queues = get_initial_queue_families(render_data, p_builder);
    exposed_fields.buffer_size = p_builder.size;
    VkBufferCreateInfo create_info = make_buffer_create_info(p_builder.size, usage, initial_queues);
    if (p_alias) {
        buffer = memory->create_buffer(create_info, memory_offset);
        return;
    }
    VkResult result = vkCreateBuffer(render_data->logic_device, &create_info, render_data->allocation_callbacks, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create Vulkan buffer: {}.", string_VkResult(result)));
    }
}

void ROBufferVK::finish_creation(const Builder& p_builder) {
    if (memory->fields().mapped_data != nullptr) {
        mapped_memory = static_cast<uint8_t*>(memory->fields().mapped_data) + memory_offset;
    }
    adopt_buffer_descriptors(render_data, p_builder, buffer, exposed_fields.buffer_size, uniform_view_index, storage_view_index);
    upload_initial_data(this, render_data, p_builder, exposed_fields.domain);
}

ROBufferVK::~ROBufferVK() {
    // Deallocate resource after frames in flight. The memory reference is released after the buffer is destroyed.
    render_data->register_delayed_call(
        [render_data = render_data, buffer = buffer, memory = memory, uniform_view_index = uniform_view_index, storage_view_index = storage_view_index]() mutable {
            if (uniform_view_index.has_value()) {
                const DescriptorArenaLocationVK arena = render_data->buffer_descriptor_arena;
                render_data->descriptor_heap_allocator->deallocate_descriptor(arena.heap_id, arena.arena_id, *uniform_view_index);
            }
            if (storage_view_index.has_value()) {
                const DescriptorArenaLocationVK arena = render_data->buffer_descriptor_arena;
                render_data->descriptor_heap_allocator->deallocate_descriptor(arena.heap_id, arena.arena_id, *storage_view_index);
            }
            vkDestroyBuffer(render_data->logic_device, buffer, render_data->allocation_callbacks);
            memory = MEM_NULL;
        },
        render_data->frames_in_flight);
}

VkDeviceAddress ROBufferVK::get_device_address() const {
    VkBufferDeviceAddressInfo address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = buffer;
    return vkGetBufferDeviceAddress(render_data->logic_device, &address_info);
}

uint32_t ROBufferVK::get_uniform_view_index() const {
    if ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == 0) {
        throw std::runtime_error("Failed to create uniform-buffer descriptor: buffer does not have uniform-buffer usage.");
    }
    if (!uniform_view_index.has_value()) {
        DescriptorHeapAllocatorVK* allocator = render_data->descriptor_heap_allocator;
        if (allocator == nullptr) {
            throw std::runtime_error("Failed to create uniform-buffer descriptor: descriptor heap allocator is unavailable.");
        }
        const DescriptorArenaLocationVK arena = render_data->buffer_descriptor_arena;
        uniform_view_index = allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
        write_buffer_descriptor(render_data, *uniform_view_index, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buffer, exposed_fields.buffer_size);
    }
    return *uniform_view_index;
}

uint32_t ROBufferVK::get_storage_view_index() const {
    if ((usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == 0) {
        throw std::runtime_error("Failed to create storage-buffer descriptor: buffer does not have storage-buffer usage.");
    }
    if (!storage_view_index.has_value()) {
        DescriptorHeapAllocatorVK* allocator = render_data->descriptor_heap_allocator;
        if (allocator == nullptr) {
            throw std::runtime_error("Failed to create storage-buffer descriptor: descriptor heap allocator is unavailable.");
        }
        const DescriptorArenaLocationVK arena = render_data->buffer_descriptor_arena;
        storage_view_index = allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
        write_buffer_descriptor(render_data, *storage_view_index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer, exposed_fields.buffer_size);
    }
    return *storage_view_index;
}

std::future<std::vector<uint8_t>> ROBufferVK::acquire_buffer_content() const {
    return schedule_device_buffer_readback(
        render_data, buffer, exposed_fields.buffer_size, usage, 0, exposed_fields.buffer_size, [](BufferReadbackOperation& p_operation) {
            std::vector<uint8_t> content(p_operation.size);
            copy_device_buffer_readback(p_operation, content);
            return content;
        });
}

std::future<void> ROBufferVK::acquire_buffer_content(std::span<uint8_t> p_destination) const {
    if (p_destination.size_bytes() < exposed_fields.buffer_size) {
        throw std::runtime_error(std::format("Failed to acquire buffer content: destination has {} bytes but {} bytes are required.",
            p_destination.size_bytes(),
            exposed_fields.buffer_size));
    }
    return acquire_buffer_content(0, p_destination.first(exposed_fields.buffer_size));
}

std::future<void> ROBufferVK::acquire_buffer_content(size_t p_source_offset, std::span<uint8_t> p_destination) const {
    return schedule_device_buffer_readback(
        render_data, buffer, exposed_fields.buffer_size, usage, p_source_offset, p_destination.size_bytes(), [p_destination](BufferReadbackOperation& p_operation) {
            copy_device_buffer_readback(p_operation, p_destination);
        });
}

DebugDataRows ROBufferVK::descriptor_debug_data() const {
    DebugDataRows rows(render_data->allocator);
    if (!uniform_view_index.has_value() && !storage_view_index.has_value()) {
        return rows;
    }
    VkDeviceAddress device_address = get_buffer_address_range(render_data, buffer, exposed_fields.buffer_size).address;
    if (uniform_view_index.has_value()) {
        rows.emplace_back(make_descriptor_row("ROBufferVK", "uniform_buffer", *uniform_view_index, device_address));
    }
    if (storage_view_index.has_value()) {
        rows.emplace_back(make_descriptor_row("ROBufferVK", "storage_buffer", *storage_view_index, device_address));
    }
    return rows;
}

void ROBufferVK::update_buffer(const void* p_data, size_t p_dst_offset, size_t p_size) {
    if (exposed_fields.domain != DeviceDomain::HOST && exposed_fields.domain != DeviceDomain::HOST_DEVICE) {
        throw std::runtime_error(
            "Failed to update buffer: the domain of the device must be declared as host to update buffer from host memory.");
    }
    if (p_dst_offset + p_size > exposed_fields.buffer_size) {
        throw std::runtime_error(std::format("Failed to update buffer: trying to map a region ({} to {}) onto a buffer with capacity {}.",
            p_dst_offset,
            p_dst_offset + p_size,
            exposed_fields.buffer_size));
    }
    memcpy(static_cast<uint8_t*>(mapped_memory) + p_dst_offset, p_data, p_size);
    memory->flush(memory_offset + p_dst_offset, p_size);
}

void ROBufferVK::update_buffer(Ref<ROBufferVK> p_data, size_t p_src_offset, size_t p_dst_offset, size_t p_size) {
    if (p_dst_offset + p_size > exposed_fields.buffer_size) {
        throw std::runtime_error(std::format("Failed to update buffer: trying to map a region ({} to {}) onto a buffer with capacity {}.",
            p_dst_offset,
            p_dst_offset + p_size,
            exposed_fields.buffer_size));
    }
    if (p_src_offset + p_size > p_data->exposed_fields.buffer_size) {
        throw std::runtime_error(std::format("Failed to update buffer: trying to read a region ({} to {}) from a buffer with capacity {}.",
            p_src_offset,
            p_src_offset + p_size,
            p_data->exposed_fields.buffer_size));
    }
    VkBufferCopy copy_region{};
    copy_region.srcOffset = p_src_offset;
    copy_region.dstOffset = p_dst_offset;
    copy_region.size = p_size;
    // Register a renderer-owned GPU job to copy the buffer before graph execution.
    RendererVK::get_singleton()->register_gpu_job(
        [src_buffer = p_data, dst_buffer = this->get_buffer(), copy_region](VkCommandBuffer p_command_buffer) {
            vkCmdCopyBuffer(p_command_buffer, src_buffer->get_buffer(), dst_buffer, 1, &copy_region);
        });
}

void ROBufferVK::update_buffer(const void* p_data, size_t p_size) {
    update_buffer(p_data, 0, p_size);
}

void ROBufferVK::update_buffer(Ref<ROBufferVK> p_data, size_t p_dst_offset, size_t p_size) {
    update_buffer(p_data, 0, p_dst_offset, p_size);
}

void ROBufferVK::update_buffer(Ref<ROBufferVK> p_data, size_t p_size) {
    update_buffer(p_data, 0, 0, p_size);
}

void ROBufferVK::update_buffer(Ref<ROBufferVK> p_data) {
    update_buffer(p_data, 0, 0, p_data->exposed_fields.buffer_size);
}

void ROBufferVK::update_buffer(Ref<ROStagingBufferVK> p_buffer, void* p_data, size_t p_size) { // NOLINT
    size_t src_offset = reinterpret_cast<uintptr_t>(p_data) - reinterpret_cast<uintptr_t>(p_buffer->mapped_memory);
    VkResult flush_result = vmaFlushAllocation(render_data->vma_allocator, p_buffer->allocation, src_offset, p_size);
    if (flush_result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to flush Vulkan staging-buffer allocation: {}.", string_VkResult(flush_result)));
    }
    VkBufferCopy copy_region{};
    copy_region.srcOffset = src_offset;
    copy_region.dstOffset = 0;
    copy_region.size = p_size;
    RendererVK::get_singleton()->register_gpu_job(
        [src_buffer = p_buffer, dst_buffer = this->get_buffer(), copy_region](VkCommandBuffer p_command_buffer) {
            vkCmdCopyBuffer(p_command_buffer, src_buffer->get_buffer(), dst_buffer, 1, &copy_region);
        });
}

VkBufferMemoryBarrier2 ROBufferVK::MemoryBarrierBuilder::build() const {
    if (buffer_handle == VK_NULL_HANDLE || buffer_size == 0) {
        throw std::runtime_error("Failed to build buffer memory barrier: buffer and buffer size are required.");
    }
    VkBufferMemoryBarrier2 result{};
    result.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    result.srcAccessMask = src_access_mask;
    result.srcStageMask = src_stage_mask;
    result.dstAccessMask = dst_access_mask;
    result.dstStageMask = dst_stage_mask;
    result.srcQueueFamilyIndex = src_queue_family_index;
    result.dstQueueFamilyIndex = dst_queue_family_index;
    result.buffer = buffer_handle;
    result.offset = offset;
    result.size = size == 0 ? buffer_size : size;
    return result;
}

void ROBufferVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_render_object(p_graph, p_resource_id, acquire_this_ref());
}

void ROBufferVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_BUFFER, buffer, p_name);
}

} // namespace WhiteBirdEngine

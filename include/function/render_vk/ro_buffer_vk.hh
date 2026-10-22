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
#ifndef WBE_FILE_RO_BUFFER_VK_HH
#define WBE_FILE_RO_BUFFER_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/function_utils.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/device_memory_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_staging_buffer_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>
#include <string_view>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

class ROBufferVK;

/**
 * @struct BufferDescVK
 * @brief Buffer creation configuration and fluent builder.
 *
 * Copyable and movable; copies duplicate owned initial bytes and moves transfer them. Descriptor indices are adopted only
 * when resource creation succeeds; descriptions do not own those indices.
 */
struct BufferDescVK {
    friend ROBufferVK;

    /**
     * @brief The size of the buffer.
     */
    size_t size = WBE_KI_B(4UL);
    /**
     * @brief The usage of the buffer.
     */
    Required<VkBufferUsageFlags> usage;
    /**
     * @brief Queue families this buffer is going to be used across. Either this
     * size is 1 or is empty will make the buffer created in exclusive
     * sharing mode, otherwise it will be created in concurrent sharing
     * mode.
     */
    std::set<uint32_t> queue_family_indices;
    /**
     * @brief The domain where this buffer's memory will be allocated.
     */
    Required<DeviceDomain> domain;
    /**
     * @brief Allocated uniform-buffer descriptor index adopted by the created buffer.
     */
    std::optional<uint32_t> uniform_view_index;
    /**
     * @brief Allocated storage descriptor index adopted by the created resource.
     */
    std::optional<uint32_t> storage_view_index;
    /**
     * @brief Initial bytes copied into the buffer during creation.
     */
    std::vector<uint8_t> initial_data_bytes;

    /**
     * @brief Configure storage for an array of equally sized elements.
     *
     * @param p_element_size Size of one element in bytes.
     * @param p_element_count Number of elements.
     * @param p_usage Vulkan usage flags for the buffer.
     * @param p_domain Memory domain used by the buffer.
     * @return *this.
     * @throws std::runtime_error If either dimension is zero or the total size overflows.
     */
    BufferDescVK& set_properties(
        size_t p_element_size, size_t p_element_count, VkBufferUsageFlags p_usage, DeviceDomain p_domain = DeviceDomain::HOST_DEVICE);

    /**
     * @brief Configure storage for one element.
     *
     * @param p_element_size Element size in bytes.
     * @param p_usage Vulkan usage flags for the buffer.
     * @param p_domain Memory domain used by the buffer.
     * @return *this.
     * @throws std::runtime_error If the element size is zero.
     */
    BufferDescVK& set_properties(size_t p_element_size, VkBufferUsageFlags p_usage, DeviceDomain p_domain = DeviceDomain::HOST_DEVICE);

    /**
     * @brief Configure the buffer and retain initial bytes for upload during creation.
     *
     * Device-local uploads automatically add transfer-destination usage and use a staging buffer.
     *
     * @param p_data Initial bytes copied into builder-owned storage.
     * @param p_domain Memory domain used by the buffer.
     * @return *this.
     * @throws std::runtime_error If the data span is empty.
     */
    BufferDescVK& initial_data(std::span<const uint8_t> p_data, DeviceDomain p_domain = DeviceDomain::HOST_DEVICE);

    /**
     * @brief Configure the buffer and retain initial bytes from an untyped memory region.
     * @param p_data Initial byte address.
     * @param p_size Initial byte count.
     * @param p_domain Memory domain used by the buffer.
     * @return *this.
     */
    BufferDescVK& initial_data(const void* p_data, size_t p_size, DeviceDomain p_domain = DeviceDomain::HOST_DEVICE);

    /**
     * @brief The domain where the memory of this buffer will be allocated.
     *
     * @param p_domain The domain.
     * @return *this.
     */
    BufferDescVK& set_domain(DeviceDomain p_domain) {
        domain = p_domain;
        return *this;
    }

    /**
     * @brief Set the size of the buffer.
     *
     * @param p_size The size to be set to.
     * @return *this.
     */
    BufferDescVK& set_size(size_t p_size) {
        size = p_size;
        return *this;
    }

    /**
     * @brief Set the usage of this buffer.
     *
     * @param p_usage The usage of this buffer.
     * @return *this.
     */
    BufferDescVK& set_usage(VkBufferUsageFlags p_usage) {
        usage = p_usage;
        return *this;
    }

    /**
     * @brief Add usage to this buffer.
     *
     * @param p_usage The usage to be added to this buffer.
     * @return *this.
     */
    BufferDescVK& add_usage(VkBufferUsageFlags p_usage) {
        if (usage.has_value()) {
            *usage |= p_usage;
        } else {
            usage = p_usage;
        }
        return *this;
    }

    /**
     * @brief Transfer an allocated uniform-buffer descriptor index to the created buffer.
     *
     * The created resource writes the descriptor and becomes responsible for releasing the index. The caller must not
     * release the index after construction succeeds.
     */
    BufferDescVK& set_uniform_view_index(uint32_t p_index) {
        uniform_view_index = p_index;
        return *this;
    }

    /**
     * @brief Transfer an allocated storage-buffer descriptor index to the created buffer.
     *
     * The created resource writes the descriptor and becomes responsible for releasing the index. The caller must not
     * release the index after construction succeeds.
     */
    BufferDescVK& set_storage_view_index(uint32_t p_index) {
        storage_view_index = p_index;
        return *this;
    }

    /**
     * @brief Set the queue families that may use this buffer.
     *
     * @param p_queue_family_indices The queue family indices that might use
     * this buffer.
     * @return *this.
     */
    BufferDescVK& set_queue_family_indices(const std::set<uint32_t>& p_queue_family_indices) {
        queue_family_indices = p_queue_family_indices;
        return *this;
    }

    /**
     * @brief Set the queue families that may use this buffer.
     *
     * @param p_queue_family_indices The queue family indices that might use
     * this buffer.
     * @return *this.
     */
    BufferDescVK& set_queue_family_indices(std::set<uint32_t>&& p_queue_family_indices) {
        queue_family_indices = std::move(p_queue_family_indices);
        return *this;
    }

    /**
     * @brief Create a copyable owning reference on the rendering thread using the renderer context.
     */
    Ref<ROBufferVK> create_ref(const RenderContextVK* p_render_data) const;

    /**
     * @brief Create a move-only owner on the rendering thread using the renderer allocator.
     */
    Unique<ROBufferVK> create_unique(const RenderContextVK* p_render_data) const;
};

/**
 * @struct BufferAliasDescVK
 * @brief Configuration for a buffer bound to shared existing device memory.
 *
 * Copyable and movable; copies share the retained device-memory reference and moves transfer it. This description does not
 * allocate a Vulkan resource.
 */
struct BufferAliasDescVK {
    /**
     * @brief Buffer configuration. Its domain, when set, must match the memory's domain.
     */
    BufferDescVK buffer;
    /**
     * @brief Memory the buffer is bound to.
     */
    Ref<DeviceMemoryVK> memory;
    /**
     * @brief Offset of the buffer inside the memory allocation.
     */
    VkDeviceSize offset = 0;

    /**
     * @brief Set the buffer configuration.
     * @param p_buffer The buffer configuration.
     * @return *this.
     */
    BufferAliasDescVK& set_buffer(const BufferDescVK& p_buffer) {
        buffer = p_buffer;
        return *this;
    }

    /**
     * @brief Set the memory the buffer aliases.
     * @param p_memory The memory.
     * @param p_offset Offset of the buffer inside the memory allocation.
     * @return *this.
     */
    BufferAliasDescVK& set_memory(Ref<DeviceMemoryVK> p_memory, VkDeviceSize p_offset = 0) {
        memory = std::move(p_memory);
        offset = p_offset;
        return *this;
    }

    /**
     * @brief Create a copyable owning reference on the rendering thread using the renderer context.
     */
    Ref<ROBufferVK> create_ref(const RenderContextVK* p_render_data) const;

    /**
     * @brief Create a move-only owner on the rendering thread using the renderer allocator.
     */
    Unique<ROBufferVK> create_unique(const RenderContextVK* p_render_data) const;
};

/**
 * @class ROBufferVK
 * @brief Buffer render object.
 */
class WBE_META(WBE_RENDER_OBJECT) ROBufferVK final : public IRenderObjectVK, public IBindableRenderResourceVK {
public:
    /**
     * @class ExposedFields
     * @brief The exposed fields of ro_buffer_vk.
     *
     */
    struct ExposedFields {
        // The size of the buffer.
        size_t buffer_size;
        // The domain where the buffer's memory is located.
        DeviceDomain domain;
    };

    /**
     * @brief Compatibility name for BufferDescVK.
     */
    using Builder = BufferDescVK;

    /**
     * @brief Compatibility name for BufferAliasDescVK.
     */
    using AliasBuilder = BufferAliasDescVK;

    /**
     * @class BufferMemoryBarrierBuilder
     * @brief Builder for buffer memory barriers.
     *
     */
    struct MemoryBarrierBuilder {
        friend ROBufferVK;
        // The source access.
        VkAccessFlags2 src_access_mask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        // The source stage.
        VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        // The destination access.
        VkAccessFlags2 dst_access_mask = VK_ACCESS_2_MEMORY_READ_BIT;
        // The destination stage.
        VkPipelineStageFlags2 dst_stage_mask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        // The source queue family to tranfer ownership. If all the ownerships
        // in ROBufferVK is set in queue_family_indices, this field could be set
        // to VK_QUEUE_FAMILY_IGNORED.
        uint32_t src_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
        // The destination queue family to tranfer ownership. If all the
        // ownerships in ROBufferVK is set in queue_family_indices, this field
        // could be set to VK_QUEUE_FAMILY_IGNORED.
        uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
        // The buffer to set barrier on.
        Ref<ROBufferVK> buffer;
        // Raw buffer handle used when the buffer is owned by another render-object type.
        VkBuffer buffer_handle = VK_NULL_HANDLE;
        // Total size of the raw buffer.
        VkDeviceSize buffer_size = 0;
        // The offset of the data to apply barrier on in the buffer.
        VkDeviceSize offset = 0;
        // The size of the data to apply barrier on in the buffer. If set to 0,
        // the barrier is going to be applied from offset to the end of barrier.
        VkDeviceSize size = 0;

        /**
         * @brief Set the source access.
         *
         * @param p_src_access_mask The access bit mask.
         * @return *this.
         */
        MemoryBarrierBuilder& set_src_access(VkAccessFlags2 p_src_access_mask) {
            src_access_mask = p_src_access_mask;
            return *this;
        }

        /**
         * @brief Set the source access.
         *
         * @param p_src_access_mask The access bit mask.
         * @param p_src_stage The source pipeline stage.
         * @return *this.
         */
        MemoryBarrierBuilder& set_src_access(VkAccessFlags2 p_src_access_mask, VkPipelineStageFlags2 p_src_stage_mask) {
            src_access_mask = p_src_access_mask;
            src_stage_mask = p_src_stage_mask;
            return *this;
        }

        /**
         * @brief Set the destination access.
         *
         * @param p_dst_access_mask The access bit mask.
         * @return *this.
         */
        MemoryBarrierBuilder& set_dst_access(VkAccessFlags2 p_dst_access_mask) {
            dst_access_mask = p_dst_access_mask;
            return *this;
        }

        /**
         * @brief Set the destination access.
         *
         * @param p_dst_access_mask The access bit mask.
         * @param p_dst_stage_mask The destination pipeline stage.
         * @return *this.
         */
        MemoryBarrierBuilder& set_dst_access(VkAccessFlags2 p_dst_access_mask, VkPipelineStageFlags2 p_dst_stage_mask) {
            dst_access_mask = p_dst_access_mask;
            dst_stage_mask = p_dst_stage_mask;
            return *this;
        }

        /**
         * @brief Set access.
         *
         * @param p_src_access_mask The source access bit mask.
         * @param p_dst_access_mask The destination access bit mask.
         * @return
         */
        MemoryBarrierBuilder& set_access(VkAccessFlags2 p_src_access_mask, VkAccessFlags2 p_dst_access_mask) {
            src_access_mask = p_src_access_mask;
            dst_access_mask = p_dst_access_mask;
            return *this;
        }

        /**
         * @brief Set the buffer to apply the barrier on.
         *
         * @param p_buffer The buffer to apply the barrier on.
         * @return *this.
         */
        MemoryBarrierBuilder& set_buffer(Ref<ROBufferVK> p_buffer) {
            buffer = p_buffer;
            buffer_handle = p_buffer->get_buffer();
            buffer_size = p_buffer->fields().buffer_size;
            return *this;
        }

        /**
         * @brief Set a raw buffer handle and its total size.
         *
         * This overload does not retain a render object. The caller must keep the buffer alive until command execution finishes.
         *
         * @param p_buffer The Vulkan buffer handle.
         * @param p_buffer_size The total buffer size in bytes.
         * @return *this.
         */
        MemoryBarrierBuilder& set_buffer(VkBuffer p_buffer, VkDeviceSize p_buffer_size) {
            WBE_DEBUG_ASSERT(p_buffer != VK_NULL_HANDLE);
            WBE_DEBUG_ASSERT(p_buffer_size > 0);
            buffer = MEM_NULL;
            buffer_handle = p_buffer;
            buffer_size = p_buffer_size;
            return *this;
        }

        /**
         * @brief Set the offset of the data region in the buffer that this
         * barrier is going to be applied on.
         *
         * @param p_offset The offset.
         * @return *this.
         */
        MemoryBarrierBuilder& set_offset(VkDeviceSize p_offset) {
            offset = p_offset;
            return *this;
        }

        /**
         * @brief Set the size of the data region in the buffer that this
         * barrier is going to be applied on.
         *
         * @param p_size The size. If set to 0, map the region from p_offset to
         * the end of the buffer.
         * @return *this.
         */
        MemoryBarrierBuilder& set_size(VkDeviceSize p_size) {
            size = p_size;
            return *this;
        }

        /**
         * @brief Set the data region in the buffer that this barrier is going
         * to be applied on.
         *
         * @param p_offset The offset.
         * @param p_size The size. If set to 0, map the region from p_offset to
         * the end of the buffer.
         * @return *this.
         */
        MemoryBarrierBuilder& set_data_region(VkDeviceSize p_offset, VkDeviceSize p_size) {
            offset = p_offset;
            size = p_size;
            return *this;
        }

        /**
         * @brief Build the barrier.
         *
         * @return The barrier instance.
         */
        VkBufferMemoryBarrier2 build() const;
    };

    /**
     * @brief Constructor. Allocates a device memory for the buffer and binds it.
     *
     * @param p_builder The builder instance.
     */
    ROBufferVK(const RenderContextVK* p_render_data, const Builder& p_builder);

    /**
     * @brief Constructor. Binds the buffer to existing device memory as an alias.
     *
     * @param p_builder The alias builder instance.
     * @throws std::runtime_error If the memory is null, its domain differs from the configured one, or it cannot hold the
     * buffer at the offset.
     */
    ROBufferVK(const RenderContextVK* p_render_data, const AliasBuilder& p_builder);
    virtual ~ROBufferVK() override;
    ROBufferVK(const ROBufferVK&) = delete;
    ROBufferVK(ROBufferVK&&) noexcept = delete;
    ROBufferVK& operator=(const ROBufferVK&) = delete;
    ROBufferVK& operator=(ROBufferVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Get the buffer instance.
     *
     * @return The buffer instance.
     */
    VkBuffer get_buffer() const {
        return buffer;
    }

    /**
     * @brief Get the Vulkan device address of the buffer.
     *
     * @return The buffer device address.
     */
    VkDeviceAddress get_device_address() const;

    /**
     * @brief Acquire the device memory the buffer is bound to, for example to alias another resource onto it.
     *
     * @return The device memory.
     */
    Ref<DeviceMemoryVK> acquire_memory() const {
        return memory;
    }

    /**
     * @brief Get the offset of the buffer inside its device memory allocation.
     *
     * @return The memory offset.
     */
    VkDeviceSize get_memory_offset() const {
        return memory_offset;
    }

    virtual DebugDataRows descriptor_debug_data() const override;

    /**
     * @brief Get or lazily create the whole-buffer uniform descriptor index.
     */
    uint32_t get_uniform_view_index() const;

    /**
     * @brief Get or lazily create the whole-buffer storage descriptor index.
     */
    uint32_t get_storage_view_index() const;

    /**
     * @brief Acquire the complete buffer as CPU bytes.
     *
     * Device-local buffers must have transfer-source usage. The returned future and this buffer must finish before renderer
     * shutdown.
     *
     * @return A future containing the buffer bytes.
     * @throws std::runtime_error If the buffer cannot be copied or no engine worker is available.
     */
    std::future<std::vector<uint8_t>> acquire_buffer_content() const;

    /**
     * @brief Acquire the complete buffer into caller-owned CPU storage.
     * @param p_destination Storage that receives the buffer bytes and remains alive until completion.
     * @return A future completed after the destination has been populated.
     * @throws std::runtime_error If the destination is too small, the buffer cannot be copied, or no engine worker is available.
     */
    std::future<void> acquire_buffer_content(std::span<uint8_t> p_destination) const;

    /**
     * @brief Acquire one byte range into caller-owned CPU storage.
     * @param p_source_offset First source byte in this buffer.
     * @param p_destination Storage that receives the selected bytes and remains alive until completion.
     * @return A future completed after the destination has been populated.
     * @throws std::runtime_error If the range is invalid, the buffer cannot be copied, or no engine worker is available.
     */
    std::future<void> acquire_buffer_content(size_t p_source_offset, std::span<uint8_t> p_destination) const;

    /**
     * @brief Update the buffer. Updating from main memory must specify the
     * device domain as host.
     *
     * @param p_data The data to copy from.
     * @param p_dst_offset Offset in the buffer that the beginning of the data
     * would be copied to.
     * @param p_size The size of the data in bytes that would be copied to the
     * buffer.
     */
    void update_buffer(const void* p_data, size_t p_dst_offset, size_t p_size);

    /**
     * @brief Update the buffer. Updating from main memory must specify the
     * device domain as host.
     *
     * @param p_data The data to be updated to.
     * @param p_size The size of data that will be transmitted.
     */
    void update_buffer(const void* p_data, size_t p_size);

    /**
     * @brief Update the buffer from another buffer object.
     *
     * @param p_data The buffer object to copy data from.
     * @param p_src_offset The offset to copy data from in p_data.
     * @param p_dst_offset The offset to copy data to in the buffer.
     * @param p_size The size to copy (in bytes).
     */
    void update_buffer(Ref<ROBufferVK> p_data, size_t p_src_offset, size_t p_dst_offset, size_t p_size);

    /**
     * @brief Update the buffer from another buffer object.
     *
     * @param p_data The buffer object to copy data from.
     * @param p_dst_offset Offset in the buffer that the beginning of the data
     * would be copied to. This has to be a multiple of 4.
     * @param p_size The size of the data in bytes that would be copied to the
     * buffer. This has to be a multiple of 4.
     */
    void update_buffer(Ref<ROBufferVK> p_data, size_t p_dst_offset, size_t p_size);

    /**
     * @brief Update the buffer from another buffer object.
     *
     * @param p_data The buffer object to copy data from.
     * @param p_size The size of the data in bytes that would be copied to the
     * buffer. This has to be a multiple of 4.
     */
    void update_buffer(Ref<ROBufferVK> p_data, size_t p_size);

    /**
     * @brief Update the buffer from another buffer object.
     *
     * @param p_data The buffer object to copy data from.
     */
    void update_buffer(Ref<ROBufferVK> p_data);

    /**
     * @brief Update the buffer from a staging buffer.
     *
     * @param p_buffer The staging buffer to retrieve data.
     * @param p_data The data to fill in.
     */
    void update_buffer(Ref<ROStagingBufferVK> p_buffer, void* p_data, size_t p_size);

    const ExposedFields& fields() const {
        return exposed_fields;
    }

    /**
     * @brief Return the Vulkan usage flags the buffer was created with.
     */
    VkBufferUsageFlags get_usage() const {
        return usage;
    }

    /**
     * @brief Import this object under p_resource_id.
     * @throws std::runtime_error If the object was not created with make_ref() or p_resource_id is empty.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    void* mapped_memory = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    Ref<DeviceMemoryVK> memory;
    VkDeviceSize memory_offset = 0;
    ExposedFields exposed_fields{};
    VkBufferUsageFlags usage = 0;
    mutable std::optional<uint32_t> uniform_view_index;
    mutable std::optional<uint32_t> storage_view_index;

    void create_buffer(const Builder& p_builder, bool p_alias);
    void finish_creation(const Builder& p_builder);
};
} // namespace WhiteBirdEngine

#endif

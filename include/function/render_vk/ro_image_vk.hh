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
#ifndef WBE_FILE_RO_IMAGE_VK_HH
#define WBE_FILE_RO_IMAGE_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/device_memory_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/ktx_vulkan_adapter.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "glm/ext/vector_uint2.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <future>
#include <set>
#include <span>
#include <utility>
#include <vector>
#include <string_view>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

class ROImageViewVK;

class ROImageVK;

/**
 * @struct ImageDescVK
 * @brief Image creation configuration and fluent builder.
 *
 * Copyable and movable value configuration; no Vulkan resource is owned by this description.
 */
struct ImageDescVK {
    friend ROImageVK;
    /**
     * @brief Format used for the depth attachment of a spot light shadow map.
     */
    static constexpr VkFormat SHADOW_MAP_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    /**
     * @brief Format used for the color attachment that accompanies a spot
     * light shadow map.
     */
    static constexpr VkFormat SHADOW_MAP_COLOR_FORMAT = VK_FORMAT_R8_UNORM;
    /**
     * @brief Image type.
     */
    Required<VkImageType> image_type;
    /**
     * @brief Image format.
     */
    Required<VkFormat> format;
    /**
     * @brief Image extent.
     */
    Required<VkExtent3D> extent;
    /**
     * @brief Mipmap level.
     */
    uint32_t mipmap_level = 1;
    /**
     * @brief Number of layers.
     */
    uint32_t layers_count = 1;
    /**
     * @brief Image usages.
     */
    Required<VkImageUsageFlags> usage;
    /**
     * @brief The queue families this image will be used in. If not set, it will set to
     * all queue families used in the graphics renderer.
     */
    std::set<uint32_t> queue_families;

    /**
     * @brief Set the image type.
     */
    ImageDescVK& set_image_type(VkImageType p_image_type);
    /**
     * @brief Set the format.
     */
    ImageDescVK& set_format(VkFormat p_format);
    /**
     * @brief Set the image extent.
     */
    ImageDescVK& set_extent(VkExtent3D p_extent);
    /**
     * @brief Set the mipmap level.
     */
    ImageDescVK& set_mipmap_level(uint32_t p_mipmap_level);
    /**
     * @brief Set the number of layers of the image.
     */
    ImageDescVK& set_layers(uint32_t p_layers_count);
    /**
     * @brief Set the image usage.
     */
    ImageDescVK& set_usage(VkImageUsageFlags p_usage);
    /**
     * @brief Set the queue families.
     */
    ImageDescVK& set_queue_families(const std::set<uint32_t>& p_queue_families);
    /**
     * @brief Configure the required properties of a two-dimensional image.
     *
     * @param p_format Image format.
     * @param p_extent Image width and height. The depth is set to one.
     * @param p_usage Image usage flags.
     * @return *this.
     */
    ImageDescVK& set_properties_2d(VkFormat p_format, VkExtent2D p_extent, VkImageUsageFlags p_usage);
    /**
     * @brief Make a 2D image.
     */
    ImageDescVK& image_2d(glm::uvec2 p_size);
    /**
     * @brief Make a 2D image.
     */
    ImageDescVK& image_2d(VkExtent2D p_extent);
    /**
     * @brief Make a 3D image.
     */
    ImageDescVK& image_3d(glm::uvec3 p_size);
    /**
     * @brief Make a 3D image.
     */
    ImageDescVK& image_3d(VkExtent3D p_extent);
    /**
     * @brief Make an RGB image with each element 8 bit wide.
     */
    ImageDescVK& rgb();
    /**
     * @brief Make an SRGB image with each element 8 bit wide.
     */
    ImageDescVK& srgb();
    /**
     * @brief Make an RGBA image with each element 8 bit wide.
     */
    ImageDescVK& rgba();
    /**
     * @brief Make an SRGBA image with each element 8 bit wide.
     */
    ImageDescVK& srgba();
    /**
     * @brief Make an RGB HDR image with each element 32 bit wide.
     */
    ImageDescVK& rgb_hdr();
    /**
     * @brief Make an RGBA HDR image with each element 32 bit wide.
     */
    ImageDescVK& rgba_hdr();

    /**
     * @brief Create a copyable owning reference on the rendering thread using the renderer context.
     */
    Ref<ROImageVK> create_ref(const RenderContextVK* p_render_data) const;

    /**
     * @brief Create a move-only owner on the rendering thread using the renderer allocator.
     */
    Unique<ROImageVK> create_unique(const RenderContextVK* p_render_data) const;
};

/**
 * @struct ImageAliasDescVK
 * @brief Configuration for an image bound to shared existing device memory.
 *
 * Copyable and movable; copies share the retained device-memory reference and moves transfer it. This description does not
 * allocate a Vulkan resource.
 */
struct ImageAliasDescVK {
    /**
     * @brief Image configuration.
     */
    ImageDescVK image;
    /**
     * @brief Memory the image is bound to.
     */
    Ref<DeviceMemoryVK> memory;
    /**
     * @brief Offset of the image inside the memory allocation.
     */
    VkDeviceSize offset = 0;

    /**
     * @brief Set the image configuration.
     * @param p_image The image configuration.
     * @return *this.
     */
    ImageAliasDescVK& set_image(const ImageDescVK& p_image) {
        image = p_image;
        return *this;
    }

    /**
     * @brief Set the memory the image aliases.
     * @param p_memory The memory.
     * @param p_offset Offset of the image inside the memory allocation.
     * @return *this.
     */
    ImageAliasDescVK& set_memory(Ref<DeviceMemoryVK> p_memory, VkDeviceSize p_offset = 0) {
        memory = std::move(p_memory);
        offset = p_offset;
        return *this;
    }

    /**
     * @brief Create a copyable owning reference on the rendering thread using the renderer context.
     */
    Ref<ROImageVK> create_ref(const RenderContextVK* p_render_data) const;

    /**
     * @brief Create a move-only owner on the rendering thread using the renderer allocator.
     */
    Unique<ROImageVK> create_unique(const RenderContextVK* p_render_data) const;
};

/**
 * @class ROImageVK
 * @brief Image.
 *
 */
class WBE_META(WBE_RENDER_OBJECT) ROImageVK final : public IRenderObjectVK, public IBindableRenderResourceVK {
    friend struct MemoryBarrierBuilder;
    friend class RendererVK;

public:
    /**
     * @brief Create a shared view matching this image's type, format, aspects, mip levels, and array layers.
     *
     * Uses identity component mapping. Each call creates a separate view on the rendering thread and retains this image.
     * @return An owning reference allocated with the renderer's allocator.
     * @throws std::runtime_error If this image is not owned by a Ref, its format is unavailable, or view creation fails.
     */
    Ref<ROImageViewVK> get_image_view_ref() const;

    /**
     * @brief Create a shared image view on the rendering thread, retaining this image until the view is destroyed.
     *
     * Each call creates a separate view.
     *
     * @param p_view_type View type.
     * @param p_format View format.
     * @param p_subresource_range Viewed subresources. A zero aspect mask inherits the image's aspects;
     * VK_REMAINING_MIP_LEVELS and VK_REMAINING_ARRAY_LAYERS cover the remaining levels and layers.
     * @param p_components Component mapping.
     * @return A copyable owning reference allocated with the renderer's allocator.
     * @throws std::runtime_error If this image is not owned by a Ref, the format is unavailable,
     * the subresource range is invalid, or view creation fails.
     */
    Ref<ROImageViewVK> get_image_view_ref(VkImageViewType p_view_type,
        VkFormat p_format,
        VkImageSubresourceRange p_subresource_range,
        VkComponentMapping p_components) const;

    /**
     * @brief Compatibility name for ImageDescVK.
     */
    using Builder = ImageDescVK;

    /**
     * @brief Compatibility name for ImageAliasDescVK.
     */
    using AliasBuilder = ImageAliasDescVK;

    ROImageVK() = delete;
    virtual ~ROImageVK() override;
    ROImageVK(const ROImageVK&) = delete;
    ROImageVK(ROImageVK&&) noexcept = delete;
    ROImageVK& operator=(const ROImageVK&) = delete;
    ROImageVK& operator=(ROImageVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Constructor. Allocates a device-local device memory for the image and binds it.
     *
     * @param p_render_data The renderer-owned context.
     * @param p_builder Configuration builder.
     */
    ROImageVK(const RenderContextVK* p_render_data, const Builder& p_builder);

    /**
     * @brief Constructor. Binds the image to existing device memory as an alias.
     *
     * @param p_render_data The renderer-owned context.
     * @param p_builder Alias configuration builder.
     * @throws std::runtime_error If the memory is null or cannot hold the image at the offset.
     */
    ROImageVK(const RenderContextVK* p_render_data, const AliasBuilder& p_builder);

    /**
     * @brief Constructor for swap chain images. This image will not manually
     * allocate memory, and will not deallocate memory when destroyed.
     *
     * @param p_render_data The renderer-owned context.
     * @param p_image The vulkan image instance.
     */
    ROImageVK(const RenderContextVK* p_render_data, VkImage p_image, VkImageLayout p_initial_image_layout);

    /**
     * @brief Adopt a Vulkan image and device-memory allocation created by libktx.
     *
     * The image is destroyed with ktxVulkanTexture_Destruct after the configured frames in flight.
     *
     * @param p_render_data The renderer-owned context.
     * @param p_texture The libktx texture allocation whose ownership is transferred to this object.
     */
    ROImageVK(const RenderContextVK* p_render_data, ktxVulkanTexture p_texture);

    /**
     * @brief Get image instance.
     *
     * @return The image instance.
     */
    VkImage get_image() const {
        return exposed_fields.image;
    }

    /**
     * @brief Acquire the device memory the image is bound to, for example to alias another resource onto it.
     *
     * Swap-chain and libktx images do not use a DeviceMemoryVK and return null.
     *
     * @return The device memory, or null.
     */
    Ref<DeviceMemoryVK> acquire_memory() const {
        return memory;
    }

    /**
     * @brief Set the data of the image immediately. This function has to be
     * called within a scope of recording command buffer.
     *
     * @param p_command_buffer The command buffer to record the image transfer.
     * @param p_data The buffer to get data from.
     * @param p_region_info The region info of the src buffer and the dst image.
     */
    void set_image_data_immediate(VkCommandBuffer p_command_buffer, Ref<ROBufferVK> p_data, VkBufferImageCopy p_region_info);

    /**
     * @brief Acquire every mip level and array layer as tightly packed CPU bytes.
     *
     * The copy is registered with the renderer and a worker waits on its completion fense. Mip levels are concatenated from
     * largest to smallest, with every layer of a mip stored contiguously. The returned future and this image must finish
     * before renderer shutdown. Multiplanar and combined depth-stencil formats are not supported.
     *
     * @return A future containing the linear image bytes.
     * @throws std::runtime_error if the image cannot be copied or no engine worker is available.
     */
    std::future<std::vector<uint8_t>> acquire_image_content() const;

    /**
     * @brief Acquire every mip level and array layer into caller-owned storage.
     *
     * The destination receives the same tightly packed byte sequence as acquire_image_content(). The destination storage and
     * this image must remain alive until the returned future completes.
     *
     * @param p_destination CPU storage that receives the image bytes.
     * @return A future completed after the destination has been populated.
     * @throws std::runtime_error If the destination is too small, the image cannot be copied, or no engine worker is available.
     */
    std::future<void> acquire_image_content(std::span<uint8_t> p_destination) const;

    /**
     * @brief Get the number of bytes acquire_image_content() produces for this image.
     *
     * @return The tightly packed byte size of every mip level and array layer.
     * @throws std::runtime_error If the image cannot be acquired.
     */
    size_t get_content_size() const;

    struct ExposedFields {
        /**
         * @brief The image instance.
         */
        VkImage image = VK_NULL_HANDLE;
        /**
         * @brief Is this a image for the swap chain.
         */
        bool is_swap_chain_image = false;
        /**
         * @brief Device-memory handle containing the image allocation.
         *
         * This handle is exposed for introspection only and must not be freed or rebound directly.
         */
        VkDeviceMemory image_memory = VK_NULL_HANDLE;
        /**
         * @brief The queue families the image is shared between.
         */
        std::set<uint32_t> queue_families;
        /**
         * @brief The image's current layout after all pending transitions.
         */
        VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        /** Format reported by the image creator, when available. */
        VkFormat format = VK_FORMAT_UNDEFINED;
        /** Image extent reported by the image creator, when available. */
        VkExtent3D extent{};
        /** Default view type for the complete image. */
        VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
        /** Number of mip levels in the image. */
        uint32_t mipmap_levels = 1;
        /** Number of array layers in the image. */
        uint32_t layers_count = 1;
        /** Usage flags supplied when the image was created or adopted. */
        VkImageUsageFlags usage = 0;
    };

    /**
     * @brief Get the exposed fields.
     *
     * @return The exposed fields.
     */
    const ExposedFields& fields() const {
        return exposed_fields;
    }

    /**
     * @class BufferMemoryBarrierBuilder
     * @brief Builder for buffer memory barriers.
     *
     */
    struct MemoryBarrierBuilder {
        friend ROImageVK;
        // The source access.
        VkAccessFlags2 src_access_mask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        // The source stage.
        VkPipelineStageFlags2 src_stage_mask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        // The destination access.
        VkAccessFlags2 dst_access_mask = VK_ACCESS_2_MEMORY_READ_BIT;
        // The destination stage.
        VkPipelineStageFlags2 dst_stage_mask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        // The old layout of the image. You can set this to
        // VK_IMAGE_LAYOUT_UNDEFINED if you don't care about the existing image
        // data.
        VkImageLayout old_layout = VK_IMAGE_LAYOUT_GENERAL;
        // The layout this barrier transitions the image into.
        VkImageLayout new_layout = VK_IMAGE_LAYOUT_GENERAL;
        // The destination queue family to tranfer ownership.
        uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
        // The image to set barrier on.
        Ref<ROImageVK> image;
        // Raw image handle used when the render object cannot safely be retained by deferred work.
        VkImage image_handle = VK_NULL_HANDLE;
        VkImageSubresourceRange subresource_range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1,};

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
         * @brief Set the image subresource range covered by this barrier.
         *
         * @param p_subresource_range The image subresource range.
         * @return *this.
         */
        MemoryBarrierBuilder& set_subresource_range(VkImageSubresourceRange p_subresource_range) {
            subresource_range = p_subresource_range;
            return *this;
        }

        /**
         * @brief Set the image subresource range from image copy layers.
         *
         * @param p_subresource_layers The image subresource layers.
         * @return *this.
         */
        MemoryBarrierBuilder& set_subresource_range(VkImageSubresourceLayers p_subresource_layers) {
            subresource_range = VkImageSubresourceRange{.aspectMask = p_subresource_layers.aspectMask,
                .baseMipLevel = p_subresource_layers.mipLevel,
                .levelCount = 1,
                .baseArrayLayer = p_subresource_layers.baseArrayLayer,
                .layerCount = p_subresource_layers.layerCount,};
            return *this;
        }

        /**
         * @brief Set the layout that the image should have after this barrier.
         *
         * This updates the tracked image layout when apply_barrier() records the transition.
         *
         * @param p_new_layout The target image layout.
         * @return *this.
         */
        MemoryBarrierBuilder& set_new_layout(VkImageLayout p_new_layout) {
            new_layout = p_new_layout;
            return *this;
        }

        /**
         * @brief Set the buffer to apply the barrier on.
         *
         * @param p_buffer The buffer to apply the barrier on.
         * @param p_drop_old Should drop the old data. The old data of the image
         * won't be kept if true. If false, old_layout information would be
         * retrieved from the image.
         * @return *this.
         */
        MemoryBarrierBuilder& set_image(Ref<ROImageVK> p_image, bool p_drop_old = false) {
            image = p_image;
            image_handle = p_image->get_image();
            if (p_drop_old) {
                // If the old data will be dropped, set the old layout to
                // VK_IMAGE_LAYOUT_UNDEFINED
                old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            } else {
                old_layout = p_image->fields().image_layout;
            }
            return *this;
        }

        /**
         * @brief Set a raw image handle and its current layout.
         *
         * This overload does not retain a render object or update its tracked layout. It is intended for deferred commands
         * that separately guarantee the Vulkan image lifetime.
         *
         * @param p_image The Vulkan image handle.
         * @param p_old_layout The image layout before this barrier.
         * @return *this.
         */
        MemoryBarrierBuilder& set_image(VkImage p_image, VkImageLayout p_old_layout) {
            WBE_DEBUG_ASSERT(p_image != VK_NULL_HANDLE);
            image = MEM_NULL;
            image_handle = p_image;
            old_layout = p_old_layout;
            return *this;
        }

        /**
         * @brief Apply this barrier to the command buffer.
         *
         * @param p_command_buffer The command buffer to push to.
         */
        void apply_barrier(VkCommandBuffer p_command_buffer);
    };


    /**
     * @brief Import this object under p_resource_id.
     * @throws std::runtime_error If the object was not created with make_ref() or p_resource_id is empty.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    enum class Ownership {
        DEVICE_MEMORY,
        KTX,
        SWAP_CHAIN,
    };

    ExposedFields exposed_fields{};
    Ref<DeviceMemoryVK> memory;
    ktxVulkanTexture ktx_texture{};
    Ownership ownership = Ownership::DEVICE_MEMORY;

    void create_image(const Builder& p_builder, Ref<DeviceMemoryVK> p_alias_memory, VkDeviceSize p_alias_offset);
};
} // namespace WhiteBirdEngine

#endif

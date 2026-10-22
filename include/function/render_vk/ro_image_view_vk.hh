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
#ifndef WBE_FILE_RO_IMAGE_VIEW_VK_HH
#define WBE_FILE_RO_IMAGE_VIEW_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class ROImageViewVK;

/**
 * @struct ImageViewDescVK
 * @brief Image-view creation configuration and fluent builder.
 *
 * Copyable and movable; copies share the retained image reference and moves transfer it. Descriptor indices are adopted only
 * when resource creation succeeds; descriptions do not own those indices.
 */
struct ImageViewDescVK {
    friend ROImageViewVK;
    /**
     * @brief The image.
     */
    Ref<ROImageVK> image;
    /**
     * @brief Image view type.
     */
    Required<VkImageViewType> view_type;
    /**
     * @brief Image format.
     */
    Required<VkFormat> format;
    /**
     * @brief Component mapping.
     */
    VkComponentMapping components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY};
    /**
     * @brief Subresource range.
     */
    VkImageSubresourceRange subresource_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    /**
     * @brief Allocated sampled-image descriptor index adopted by the created image view.
     */
    std::optional<uint32_t> sampled_view_index;
    /**
     * @brief Allocated storage descriptor index adopted by the created resource.
     */
    std::optional<uint32_t> storage_view_index;

    /**
     * @brief Set the source image.
     */
    ImageViewDescVK& set_image(Ref<ROImageVK> p_image);
    /**
     * @brief Set the image view type.
     */
    ImageViewDescVK& set_view_type(VkImageViewType p_view_type);
    /**
     * @brief Set the image format.
     */
    ImageViewDescVK& set_format(VkFormat p_format);
    /**
     * @brief Set component mapping.
     */
    ImageViewDescVK& set_components(const VkComponentMapping& p_components);
    /**
     * @brief Set component mapping.
     */
    ImageViewDescVK& set_components(VkComponentSwizzle p_r, VkComponentSwizzle p_g, VkComponentSwizzle p_b, VkComponentSwizzle p_a);
    /**
     * @brief Set subresource range.
     */
    ImageViewDescVK& set_subresource_range(const VkImageSubresourceRange& p_subresource_range);
    /**
     * @brief Set aspect mask of the subresource range.
     */
    ImageViewDescVK& set_aspect_mask(VkImageAspectFlags p_aspect_mask);
    /**
     * @brief Set mip level count.
     */
    ImageViewDescVK& set_mip_level_count(uint32_t p_level_count);
    /**
     * @brief Set from image configurations. All features are default to match to
     * the image config.
     */
    ImageViewDescVK& set_from_image_config(const ImageDescVK& p_image_config);
    /**
     * @brief Transfer an allocated sampled-image descriptor index to the created image view.
     *
     * The created resource writes the descriptor and becomes responsible for releasing the index. The caller must not
     * release the index after construction succeeds.
     */
    ImageViewDescVK& set_sampled_view_index(uint32_t p_index);
    /**
     * @brief Transfer an allocated storage-image descriptor index to the created image view.
     *
     * The created resource writes the descriptor and becomes responsible for releasing the index. The caller must not
     * release the index after construction succeeds.
     */
    ImageViewDescVK& set_storage_view_index(uint32_t p_index);

    /**
     * @brief Create a copyable owning reference on the rendering thread using the renderer context.
     */
    Ref<ROImageViewVK> create_ref(const RenderContextVK* p_render_data) const;

    /**
     * @brief Create a move-only owner on the rendering thread using the renderer allocator.
     */
    Unique<ROImageViewVK> create_unique(const RenderContextVK* p_render_data) const;

private:
    VkImageView create_vk_object(const RenderContextVK* p_render_data) const;
};

/**
 * @class ROImageViewVK
 * @brief Vulkan image view object.
 *
 */
class WBE_META(WBE_RENDER_OBJECT) ROImageViewVK final : public IRenderObjectVK, public IBindableRenderResourceVK {
public:
    /**
     * @brief Compatibility name for ImageViewDescVK.
     */
    using Builder = ImageViewDescVK;

    struct AttachmentBuilder {
        // The image view to use as the attachment.
        Required<VkImageView> image_view;
        // The resolve mode for the attachment.
        VkResolveModeFlagBits resolve_mode = VK_RESOLVE_MODE_NONE;
        // The image view to use as the resolve target.
        VkImageView resolve_image_view = VK_NULL_HANDLE;
        // The load operation to the attachment.
        VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // The store operation to the attachment.
        VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
        // The clear value to use when load_op is VK_ATTACHMENT_LOAD_OP_CLEAR.
        VkClearValue clear_value{};

        /**
         * @brief Create a builder for a color attachment.
         *
         * Default configuration:
         * - resolveMode: VK_RESOLVE_MODE_NONE
         * - loadOp: VK_ATTACHMENT_LOAD_OP_CLEAR
         * - storeOp: VK_ATTACHMENT_STORE_OP_STORE
         *
         * @param p_image_view The image view of the color attachment.
         * @return The initialized builder.
         */
        static AttachmentBuilder color(VkImageView p_image_view) {
            return AttachmentBuilder{}
                .set_image_view(p_image_view)
                .set_resolve_mode(VK_RESOLVE_MODE_NONE)
                .set_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR)
                .set_store_op(VK_ATTACHMENT_STORE_OP_STORE);
        }

        /**
         * @brief Create a builder for a depth/stencil attachment.
         *
         * Default configuration:
         * - resolveMode: VK_RESOLVE_MODE_NONE
         * - loadOp: VK_ATTACHMENT_LOAD_OP_CLEAR
         * - storeOp: VK_ATTACHMENT_STORE_OP_DONT_CARE
         *
         * @param p_image_view The image view of the depth/stencil attachment.
         * @return The initialized builder.
         */
        static AttachmentBuilder depth(VkImageView p_image_view) {
            return AttachmentBuilder{}
                .set_image_view(p_image_view)
                .set_resolve_mode(VK_RESOLVE_MODE_NONE)
                .set_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR)
                .set_store_op(VK_ATTACHMENT_STORE_OP_DONT_CARE);
        }

        /**
         * @brief Create a builder for a resolve attachment.
         *
         * Default configuration:
         * - resolveMode: VK_RESOLVE_MODE_AVERAGE_BIT
         * - loadOp: VK_ATTACHMENT_LOAD_OP_CLEAR
         * - storeOp: VK_ATTACHMENT_STORE_OP_STORE
         *
         * @param p_image_view The multisampled image view of the color attachment.
         * @param p_resolve_image_view The image view of the resolve target.
         * @return The initialized builder.
         */
        static AttachmentBuilder resolve(VkImageView p_image_view, VkImageView p_resolve_image_view) {
            return AttachmentBuilder{}
                .set_image_view(p_image_view)
                .set_resolve_mode(VK_RESOLVE_MODE_AVERAGE_BIT)
                .set_resolve_image_view(p_resolve_image_view)
                .set_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR)
                .set_store_op(VK_ATTACHMENT_STORE_OP_STORE);
        }

        /**
         * @brief Create a builder for an input attachment.
         *
         * Default configuration:
         * - resolveMode: VK_RESOLVE_MODE_NONE
         * - loadOp: VK_ATTACHMENT_LOAD_OP_LOAD
         * - storeOp: VK_ATTACHMENT_STORE_OP_DONT_CARE
         *
         * @param p_image_view The image view of the input attachment.
         * @return The initialized builder.
         */
        static AttachmentBuilder input(VkImageView p_image_view) {
            return AttachmentBuilder{}
                .set_image_view(p_image_view)
                .set_resolve_mode(VK_RESOLVE_MODE_NONE)
                .set_load_op(VK_ATTACHMENT_LOAD_OP_LOAD)
                .set_store_op(VK_ATTACHMENT_STORE_OP_DONT_CARE);
        }

        /**
         * @brief Set the image view of the attachment.
         *
         * @param p_image_view The image view of the attachment.
         * @return *this
         */
        AttachmentBuilder& set_image_view(VkImageView p_image_view) {
            image_view = p_image_view;
            return *this;
        }

        /**
         * @brief Set the resolve mode.
         *
         * @param p_resolve_mode The resolve mode.
         * @return *this
         */
        AttachmentBuilder& set_resolve_mode(VkResolveModeFlagBits p_resolve_mode) {
            resolve_mode = p_resolve_mode;
            return *this;
        }

        /**
         * @brief Set the resolve image view.
         *
         * @param p_resolve_image_view The resolve image view.
         * @return *this
         */
        AttachmentBuilder& set_resolve_image_view(VkImageView p_resolve_image_view) {
            resolve_image_view = p_resolve_image_view;
            return *this;
        }

        /**
         * @brief Set the load operation.
         *
         * @param p_load_op The load operation.
         * @return *this
         */
        AttachmentBuilder& set_load_op(VkAttachmentLoadOp p_load_op) {
            load_op = p_load_op;
            return *this;
        }

        /**
         * @brief Set the store operation.
         *
         * @param p_store_op The store operation.
         * @return *this
         */
        AttachmentBuilder& set_store_op(VkAttachmentStoreOp p_store_op) {
            store_op = p_store_op;
            return *this;
        }

        /**
         * @brief Set the clear value.
         *
         * @param p_clear_value The clear value.
         * @return *this
         */
        AttachmentBuilder& set_clear_value(const VkClearValue& p_clear_value) {
            clear_value = p_clear_value;
            return *this;
        }

        /**
         * @brief Build a Vulkan rendering attachment info.
         *
         * @return The initialized rendering attachment info.
         */
        VkRenderingAttachmentInfo build() const {
            return VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = WBE_REQUIRED(image_view),
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                .resolveMode = resolve_mode,
                .resolveImageView = resolve_image_view,
                .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
                .loadOp = load_op,
                .storeOp = store_op,
                .clearValue = clear_value,
            };
        }
    };

    ROImageViewVK() = delete;
    virtual ~ROImageViewVK() override;
    ROImageViewVK(const ROImageViewVK&) = delete;
    ROImageViewVK(ROImageViewVK&&) noexcept = delete;
    ROImageViewVK& operator=(const ROImageViewVK&) = delete;
    ROImageViewVK& operator=(ROImageViewVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Constructor.
     *
     * @param p_render_data The renderer-owned context.
     * @param p_builder Configuration builder.
     */
    ROImageViewVK(const RenderContextVK* p_render_data, const Builder& p_builder);

    /**
     * @brief Get the image view instance.
     *
     * @return The image view instance.
     */
    VkImageView get_image_view() const {
        return image_view;
    }

    virtual DebugDataRows descriptor_debug_data() const override;

    /**
     * @brief Get or lazily create the sampled-image descriptor index.
     */
    uint32_t get_sampled_view_index() const;

    /**
     * @brief Get or lazily create the storage-image descriptor index.
     */
    uint32_t get_storage_view_index() const;

    /**
     * @class ExposedFields
     * @brief Exposed fields of ROImageViewVK.
     *
     */
    struct ExposedFields {
        /**
         * @brief The source image.
         */
        Ref<ROImageVK> image = MEM_NULL;
        /**
         * @brief The viewed image subresource range.
         *
         * Used by render-graph layout transitions so barriers cover the same image subresource this view exposes.
         */
        VkImageSubresourceRange subresource_range{.baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    };

    const ExposedFields& fields() const {
        return exposed_fields;
    }


    /**
     * @brief Import this object under p_resource_id.
     * @throws std::runtime_error If the object was not created with make_ref() or p_resource_id is empty.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    VkImageView image_view = VK_NULL_HANDLE;

    ExposedFields exposed_fields{};
    VkImageViewCreateInfo create_info{};
    mutable std::optional<uint32_t> sampled_view_index;
    mutable std::optional<uint32_t> storage_view_index;

    void init(const Builder& p_builder);
};
} // namespace WhiteBirdEngine

#endif

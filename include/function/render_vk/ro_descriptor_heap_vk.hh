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
#ifndef WBE_FILE_RO_DESCRIPTOR_HEAP_VK_HH
#define WBE_FILE_RO_DESCRIPTOR_HEAP_VK_HH

#include "core/memory/reference_strong.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include "utils/utils.hh"

#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class DescriptorHeapAllocatorVK;

/**
 * @enum DescriptorHeapTypeVK
 * @brief Logical descriptor arena represented by a descriptor heap object.
 */
enum class DescriptorHeapTypeVK : uint8_t {
    RESOURCE,
    SAMPLER,
};

/**
 * @class RODescriptorHeapVK
 * @brief Logical descriptor arena backed by a Vulkan descriptor heap buffer.
 *
 * Resource arenas may share one physical backing buffer. An arena's indices are
 * offsets in the descriptor-type-specific SPIR-V view of that backing buffer.
 *
 * This object is neither copyable nor movable.
 */
class WBE_META(WBE_RENDER_OBJECT) RODescriptorHeapVK final : public IRenderObjectVK {
public:
    /**
     * @class Backing
     * @brief Shared physical storage used by logical descriptor arenas.
     */
    class Backing final {
    public:
        Backing() = delete;
        ~Backing();
        Backing(const Backing&) = delete;
        Backing(Backing&&) noexcept = delete;
        Backing& operator=(const Backing&) = delete;
        Backing& operator=(Backing&&) noexcept = delete;

        /**
         * @brief Create aligned host-visible and device-local heap storage.
         */
        Backing(const RenderContextVK* p_render_data, VkDeviceSize p_usable_size, VkDeviceSize p_reserved_size, VkDeviceSize p_alignment);

        /**
         * @brief Get the physical range used when binding this heap.
         */
        VkBindHeapInfoEXT get_bind_info() const;

    private:
        friend class RODescriptorHeapVK;
        const RenderContextVK* render_data = nullptr;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped_memory = nullptr;
        VkDeviceAddress device_address = 0;
        VkDeviceSize usable_size = 0;
        VkDeviceSize reserved_size = 0;
    };

    RODescriptorHeapVK() = delete;
    virtual ~RODescriptorHeapVK() override;
    RODescriptorHeapVK(const RODescriptorHeapVK&) = delete;
    RODescriptorHeapVK(RODescriptorHeapVK&&) noexcept = delete;
    RODescriptorHeapVK& operator=(const RODescriptorHeapVK&) = delete;
    RODescriptorHeapVK& operator=(RODescriptorHeapVK&&) noexcept = delete;

    /**
     * @brief Construct a logical arena over descriptor-heap backing storage.
     */
    RODescriptorHeapVK(const RenderContextVK* p_render_data,
        Ref<Backing> p_backing,
        DescriptorHeapTypeVK p_type,
        uint32_t p_first_index,
        uint32_t p_capacity,
        VkDeviceSize p_stride);

    virtual TypeID get_type_uuid() const override;

    /**
     * @brief Get the logical arena type.
     */
    DescriptorHeapTypeVK get_type() const {
        return type;
    }

    /**
     * @brief Get the first descriptor index owned by this arena's typed heap view.
     */
    uint32_t get_first_index() const {
        return first_index;
    }

    /**
     * @brief Get the number of descriptors in this arena.
     */
    uint32_t get_capacity() const {
        return capacity;
    }

    /**
     * @brief Get the byte stride between descriptors in this arena.
     */
    VkDeviceSize get_stride() const {
        return stride;
    }

    /**
     * @brief Check whether a descriptor-index range belongs to this arena's typed heap view.
     */
    bool owns_range(uint32_t p_first_index, uint32_t p_count = 1) const;

private:
    friend class DescriptorHeapAllocatorVK;

    void write_resource_descriptor(uint32_t p_index, const VkResourceDescriptorInfoEXT& p_descriptor) const;
    void write_sampler_descriptor(uint32_t p_index, const VkSamplerCreateInfo& p_sampler) const;
    void clear_descriptor(uint32_t p_index) const;
    VkBindHeapInfoEXT get_bind_info() const;

    Ref<Backing> backing;
    DescriptorHeapTypeVK type = DescriptorHeapTypeVK::RESOURCE;
    uint32_t first_index = 0;
    uint32_t capacity = 0;
    VkDeviceSize stride = 0;
};

} // namespace WhiteBirdEngine

#endif

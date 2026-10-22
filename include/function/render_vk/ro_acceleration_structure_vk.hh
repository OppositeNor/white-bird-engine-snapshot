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
#ifndef WBE_FILE_RO_ACCELERATION_STRUCTURE_VK_HH
#define WBE_FILE_RO_ACCELERATION_STRUCTURE_VK_HH

#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/i_ro_ray_tracing_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class ROAccelerationStructureVK
 * @brief Owns a Vulkan acceleration structure and its storage buffer.
 *
 * The owned Vulkan resources are neither copyable nor movable.
 */
class WBE_META(WBE_RENDER_OBJECT) ROAccelerationStructureVK final : public IRORayTracingVK, public IBindableRenderResourceVK {
public:
    /**
     * @class Builder
     * @brief Builder for Vulkan acceleration-structure render objects.
     */
    struct Builder {
        friend ROAccelerationStructureVK;

        /**
         * @brief Required acceleration-structure storage size.
         */
        Required<VkDeviceSize> size;
        /**
         * @brief Vulkan acceleration-structure type.
         */
        Required<VkAccelerationStructureTypeKHR> type;

        /**
         * @brief Set the required acceleration-structure storage size.
         *
         * @param p_size Storage size in bytes.
         * @return *this.
         */
        Builder& set_size(VkDeviceSize p_size) {
            size = p_size;
            return *this;
        }

        /**
         * @brief Set the Vulkan acceleration-structure type.
         *
         * @param p_type Acceleration-structure type.
         * @return *this.
         */
        Builder& set_type(VkAccelerationStructureTypeKHR p_type) {
            type = p_type;
            return *this;
        }

        /**
         * @brief Create a reference-counted acceleration-structure object.
         *
         * @param p_render_data Base render data.
         * @param p_ray_tracing_data Ray-tracing procedure table owned by the active plugin.
         * @return Reference-counted acceleration-structure object.
         */
        Ref<ROAccelerationStructureVK> create_ref(
            const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data) const {
            return make_ref<ROAccelerationStructureVK>(p_render_data->allocator, p_render_data, p_ray_tracing_data, *this);
        }

        /**
         * @brief Create a unique-ownership acceleration-structure object.
         *
         * @param p_render_data Base render data.
         * @param p_ray_tracing_data Ray-tracing procedure table owned by the active plugin.
         * @return Unique-ownership acceleration-structure object.
         */
        Unique<ROAccelerationStructureVK> create_unique(
            const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data) const {
            return make_unique<ROAccelerationStructureVK>(p_render_data->allocator, p_render_data, p_ray_tracing_data, *this);
        }
    };

    ROAccelerationStructureVK() = delete;
    /**
     * @brief Create an acceleration structure with device-local storage.
     *
     * @param p_render_data Renderer data used to create and destroy Vulkan resources.
     * @param p_ray_tracing_data Ray-tracing procedure table owned by the active plugin.
     * @param p_builder Builder containing the acceleration-structure configuration.
     */
    ROAccelerationStructureVK(
        const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data, const Builder& p_builder);
    virtual ~ROAccelerationStructureVK() override;
    ROAccelerationStructureVK(const ROAccelerationStructureVK&) = delete;
    ROAccelerationStructureVK(ROAccelerationStructureVK&&) noexcept = delete;
    ROAccelerationStructureVK& operator=(const ROAccelerationStructureVK&) = delete;
    ROAccelerationStructureVK& operator=(ROAccelerationStructureVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    /**
     * @brief Name the acceleration structure, and register its storage buffer as `<p_name>.storage`.
     */
    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Get the owned Vulkan acceleration-structure handle.
     */
    VkAccelerationStructureKHR get_acceleration_structure() const {
        return acceleration_structure;
    }

    /**
     * @brief Get the Vulkan device address of the acceleration structure.
     *
     * @return The acceleration-structure device address.
     */
    VkDeviceAddress get_device_address() const;

    /**
     * @brief Get or lazily create the acceleration-structure descriptor index.
     *
     * @return The resource-heap descriptor index.
     */
    uint32_t get_descriptor_index() const;

    virtual DebugDataRows descriptor_debug_data() const override;


    /**
     * @brief Import this object under p_resource_id.
     * @throws std::runtime_error If the object was not created with make_ref() or p_resource_id is empty.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    Ref<ROBufferVK> storage_buffer;
    mutable std::optional<uint32_t> descriptor_index;
    VkAccelerationStructureKHR acceleration_structure = VK_NULL_HANDLE;
    PFN_vkDestroyAccelerationStructureKHR destroy_acceleration_structure = nullptr;
};

} // namespace WhiteBirdEngine

#endif

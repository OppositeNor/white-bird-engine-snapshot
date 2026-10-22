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
#ifndef WBE_FILE_RT_GENERATE_MIPMAPS_VK_HH
#define WBE_FILE_RT_GENERATE_MIPMAPS_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/rt_params_generate_mipmaps_vk.hh"
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RTGenerateMipmapsVK
 * @brief Generates a color image's mip chain with filtered Vulkan blits.
 *
 * The underlying image must contain every requested mip level and support
 * transfer-source, transfer-destination, and linear blit operations. The task
 * reads mip level zero through a source view and writes the remaining levels
 * through a mip-chain view of the same image, and reports both accesses to the
 * render graph. Each level divides every dimension of the previous one by the
 * downsample rate, rounding down to at least 1; the depth of a 3D image is
 * taken from the image itself. Instances are neither copyable nor movable.
 */
struct WBE_META(WBE_RENDER_TASK(generate_mipmaps), WBE_SERIALIZATION_ALIASES(generate_mipmaps)) RTGenerateMipmapsVK final
    : public IRenderTaskVK {
    RTGenerateMipmapsVK() = delete;
    virtual ~RTGenerateMipmapsVK() override = default;
    RTGenerateMipmapsVK(const RTGenerateMipmapsVK&) = delete;
    RTGenerateMipmapsVK(RTGenerateMipmapsVK&&) noexcept = delete;
    RTGenerateMipmapsVK& operator=(const RTGenerateMipmapsVK&) = delete;
    RTGenerateMipmapsVK& operator=(RTGenerateMipmapsVK&&) noexcept = delete;

    /**
     * @brief Construct a mip-generation task.
     *
     * @param p_render_data Base Vulkan render data.
     * @param p_source_view_id Render-graph resource id of a view whose range includes mip level zero.
     * @param p_mip_chain_view_id Render-graph resource id of a view of the same image whose range includes every mip level.
     * @param p_base_extent Extent of mip level zero.
     * @param p_mipmap_level_count Total number of mip levels, including level zero.
     * @param p_format Image format used for capability validation.
     * @param p_downsample_rate Factor by which each level shrinks every dimension of the previous one.
     * @throws std::runtime_error If the format cannot perform filtered blits, the downsample rate is below 2, or the mip
     * level count exceeds the levels the base extent supports at that rate.
     */
    RTGenerateMipmapsVK(const RenderContextVK* p_render_data,
        std::string_view p_source_view_id,
        std::string_view p_mip_chain_view_id,
        VkExtent2D p_base_extent,
        uint32_t p_mipmap_level_count,
        VkFormat p_format,
        uint32_t p_downsample_rate = 2);

    /**
     * @brief Create the task from recipe parameters; used by RenderTaskRegistryVK.
     * @param p_render_data Base render data whose RenderDatabase holds the compiled pipelines.
     * @param p_params The recipe parameters.
     * @param p_context The graph context the task is created for.
     * @return The task.
     * @throws std::runtime_error If the render data or its RenderDatabase is unavailable, or the base extent cannot be
     * resolved. The constructor's own validation also applies.
     */
    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsGenerateMipmapsVK& p_params, const RendGraphContextVK& p_context);

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const override;

    virtual void bind_resources(const RendResViewVK& p_resources) override;

    virtual void record(VkCommandBuffer p_command_buffer, double p_delta) override;

private:
    const RenderContextVK* render_data = nullptr;
    std::string source_view_id;
    std::string mip_chain_view_id;
    Ref<RenderObjectContainerVK> source_view_container;
    Ref<RenderObjectContainerVK> mip_chain_view_container;
    VkExtent2D base_extent{};
    uint32_t mipmap_level_count = 1;
    uint32_t downsample_rate = 2;
};

} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_RENDER_TASK_REGISTRY_VK_TEST_TASKS_HH
#define WBE_FILE_RENDER_TASK_REGISTRY_VK_TEST_TASKS_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_graph_context_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

// Parameters of RTRegistryProbeVK.
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION) RTParamsRegistryProbeVK {
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string label;
    WBE_META(WBE_REFLECT)
    uint32_t count = 1;
};

// A GPU-free render task registered through RenderTaskRegistryVK; it records what create() received.
class WBE_META(WBE_RENDER_TASK, WBE_SERIALIZATION_ALIASES(registry_probe, registry_probe_alias)) RTRegistryProbeVK final
    : public IRenderTaskVK {
public:
    RTRegistryProbeVK() = delete;
    RTRegistryProbeVK(const RenderContextVK* p_render_data, RTParamsRegistryProbeVK p_params, VkExtent2D p_extent)
        : render_data(p_render_data), params(std::move(p_params)), extent(p_extent) {
    }
    virtual ~RTRegistryProbeVK() override = default;
    RTRegistryProbeVK(const RTRegistryProbeVK&) = delete;
    RTRegistryProbeVK(RTRegistryProbeVK&&) noexcept = delete;
    RTRegistryProbeVK& operator=(const RTRegistryProbeVK&) = delete;
    RTRegistryProbeVK& operator=(RTRegistryProbeVK&&) noexcept = delete;

    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsRegistryProbeVK& p_params, const RendGraphContextVK& p_context) {
        if (p_params.count == 0) {
            throw std::runtime_error("RTRegistryProbeVK: count must not be zero.");
        }
        return make_unique<RTRegistryProbeVK>(p_render_data->allocator, p_render_data, p_params, p_context.swap_chain_extent);
    }

    const RenderContextVK* get_render_data() const {
        return render_data;
    }

    const RTParamsRegistryProbeVK& get_params() const {
        return params;
    }

    VkExtent2D get_extent() const {
        return extent;
    }

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& /*p_reads*/, Vector<ResAccessVK, RenderAllocator>& /*p_writes*/) const override {
    }

    virtual void record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) override {
    }

private:
    const RenderContextVK* render_data;
    RTParamsRegistryProbeVK params;
    VkExtent2D extent;
};

// A GPU-free render task whose prepare_graph() declares a graph buffer named after its label and aliases it for the task.
class WBE_META(WBE_RENDER_TASK, WBE_SERIALIZATION_ALIASES(registry_preparing_probe)) RTRegistryPreparingProbeVK final : public IRenderTaskVK {
public:
    static constexpr std::string_view INPUT_ID = "probe_input";

    RTRegistryPreparingProbeVK() = default;
    virtual ~RTRegistryPreparingProbeVK() override = default;
    RTRegistryPreparingProbeVK(const RTRegistryPreparingProbeVK&) = delete;
    RTRegistryPreparingProbeVK(RTRegistryPreparingProbeVK&&) noexcept = delete;
    RTRegistryPreparingProbeVK& operator=(const RTRegistryPreparingProbeVK&) = delete;
    RTRegistryPreparingProbeVK& operator=(RTRegistryPreparingProbeVK&&) noexcept = delete;

    WBE_META(WBE_REFLECT)
    static Unique<IRenderTaskVK> create(
        const RenderContextVK* p_render_data, const RTParamsRegistryProbeVK& /*p_params*/, const RendGraphContextVK& /*p_context*/) {
        return make_unique<RTRegistryPreparingProbeVK>(p_render_data->allocator);
    }

    static void prepare_graph(
        RendGraphDescVK& p_graph, RendTaskDescVK& p_task, const RTParamsRegistryProbeVK& p_params, const RenderContextVK* /*p_render_data*/) {
        const std::string buffer_id = p_params.label + ".prepared";
        if (!p_graph.contains_resource(buffer_id)) {
            p_graph.add_buffer(buffer_id, [](const RendGraphContextVK& /*p_context*/) {
                return RendBufferDescVK{.size = 16};
            });
        }
        p_task.alias(INPUT_ID, buffer_id);
    }

    virtual QueueFamilyVK queue_family() const override {
        return QueueFamilyVK::GRAPHICS;
    }

    virtual void append_accesses(Vector<ResAccessVK, RenderAllocator>& /*p_reads*/, Vector<ResAccessVK, RenderAllocator>& /*p_writes*/) const override {
    }

    virtual void record(VkCommandBuffer /*p_command_buffer*/, double /*p_delta*/) override {
    }
};

} // namespace WhiteBirdEngine

#endif

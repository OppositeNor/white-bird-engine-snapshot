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
#ifndef WBE_FILE_PIPELINE_DESCRIPTOR_BINDINGS_VK_HH
#define WBE_FILE_PIPELINE_DESCRIPTOR_BINDINGS_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_render_task_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include <bit>
#include <cstdint>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class RendResViewVK;

/**
 * @class PushConstantValueVK
 * @brief Typed 32-bit value for a custom WBE_PUSH_CONSTANT field.
 *
 * The type must match the reflected scalar type of every field receiving the value. Copyable and movable.
 */
struct PushConstantValueVK {
    /** Scalar type of the value. */
    ShaderPushConstantTypeVK type = ShaderPushConstantTypeVK::UINT32;
    /** Raw 32-bit representation copied into push data. */
    uint32_t bits = 0;

    /**
     * @brief Create a value for a shader `uint` field.
     */
    static PushConstantValueVK from_uint32(uint32_t p_value) {
        return {.type = ShaderPushConstantTypeVK::UINT32, .bits = p_value};
    }

    /**
     * @brief Create a value for a shader `int` field.
     */
    static PushConstantValueVK from_int32(int32_t p_value) {
        return {.type = ShaderPushConstantTypeVK::INT32, .bits = std::bit_cast<uint32_t>(p_value)};
    }

    /**
     * @brief Create a value for a shader `float` field.
     */
    static PushConstantValueVK from_float32(float p_value) {
        return {.type = ShaderPushConstantTypeVK::FLOAT32, .bits = std::bit_cast<uint32_t>(p_value)};
    }
};

/**
 * @class PipelineDescriptorBindingsVK
 * @brief Resolution and push-data storage for one reflected pipeline descriptor layout.
 *
 * The object is neither copyable nor movable. It retains graph resource containers and resolves their current frame slots
 * while recording on the rendering thread.
 */
class PipelineDescriptorBindingsVK {
public:
    PipelineDescriptorBindingsVK() = delete;
    ~PipelineDescriptorBindingsVK() = default;
    PipelineDescriptorBindingsVK(const PipelineDescriptorBindingsVK&) = delete;
    PipelineDescriptorBindingsVK(PipelineDescriptorBindingsVK&&) noexcept = delete;
    PipelineDescriptorBindingsVK& operator=(const PipelineDescriptorBindingsVK&) = delete;
    PipelineDescriptorBindingsVK& operator=(PipelineDescriptorBindingsVK&&) noexcept = delete;

    /**
     * @brief Construct binding state for a reflected descriptor layout.
     * @param p_render_data Renderer state used for frame-slot resolution and push-data commands.
     * @param p_layout Immutable pipeline descriptor layout.
     */
    PipelineDescriptorBindingsVK(const RenderContextVK* p_render_data, const ShaderDescriptorHeapLayoutVK& p_layout);

    /**
     * @brief Append the reflected shader accesses of every descriptor field.
     *
     * Accesses use the reflected resource identifiers, which the render graph resolves through the task's resource map.
     * Samplers are immutable and are not reported.
     *
     * @param p_reads Receives read accesses.
     * @param p_writes Receives write accesses.
     */
    void append_accesses(Vector<ResAccessVK, RenderAllocator>& p_reads, Vector<ResAccessVK, RenderAllocator>& p_writes) const;

    /**
     * @brief Resolve and retain every reflected logical resource from the graph resource view.
     * @param p_resources Graph-global resource view.
     */
    void bind_resources(const RendResViewVK& p_resources);

    /**
     * @brief Set every custom WBE_PUSH_CONSTANT field with the identifier, if this pipeline declares any.
     *
     * The value is retained and pushed by every later push() call until it is set again.
     *
     * @param p_name Custom push-constant identifier.
     * @param p_value Value whose type must match each reflected field.
     * @return Whether this pipeline declares the identifier.
     * @throws std::runtime_error If a declared field has a different scalar type.
     */
    bool try_set_push_constant(std::string_view p_name, PushConstantValueVK p_value);

    /**
     * @brief Set every custom WBE_PUSH_CONSTANT field with the identifier.
     *
     * @param p_name Custom push-constant identifier.
     * @param p_value Value whose type must match each reflected field.
     * @throws std::runtime_error If this pipeline does not declare the identifier or a field has a different scalar type.
     */
    void set_push_constant(std::string_view p_name, PushConstantValueVK p_value);

    /**
     * @brief Resolve the current frame slot and push descriptor indices, renderer default values, and custom values.
     * @param p_command_buffer Command buffer receiving push data.
     * @throws std::runtime_error If a binding is unresolved or a custom push constant was never set.
     */
    void push(VkCommandBuffer p_command_buffer);

private:
    const RenderContextVK* render_data;
    ShaderDescriptorHeapLayoutVK layout;
    Vector<uint8_t, RenderAllocator> push_data;
    Vector<Ref<RenderObjectContainerVK>, RenderAllocator> containers;
    Vector<bool, RenderAllocator> custom_values_set;
};

} // namespace WhiteBirdEngine

#endif

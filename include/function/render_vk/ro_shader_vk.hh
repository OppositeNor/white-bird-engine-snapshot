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
#ifndef WBE_FILE_RO_SHADER_VK_HH
#define WBE_FILE_RO_SHADER_VK_HH

#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

class ROShaderVK;

/**
 * @struct ShaderDescVK
 * @brief Resource creation description; copyable and movable.
 * Copying duplicates the owned SPIR-V words; moving transfers them. Source resources need not outlive this description.
 */
struct ShaderDescVK {
    friend ROShaderVK;
    // The source code of the shader.
    Required<std::vector<uint32_t>> code;
    // Size of the code in SPIR-V words.
    Required<size_t> code_size;

    // Set the shader source code.
    ShaderDescVK& set_code(std::span<const uint32_t> p_code);

    // Set from a resource ID.
    ShaderDescVK& set_code(std::string_view p_resource_id);

    // Set from a resource.
    ShaderDescVK& set_code(RefRes<RUShader> p_resource);

    /**
     * @brief Create a reference-counted resource on the rendering thread.
     * @param p_render_context Context that outlives the created resource.
     */
    Ref<ROShaderVK> create_ref(const RenderContextVK* p_render_context) const;

    /**
     * @brief Create a uniquely owned resource on the rendering thread.
     * @param p_render_context Context that outlives the created resource.
     */
    Unique<ROShaderVK> create_unique(const RenderContextVK* p_render_context) const;

private:
    VkShaderModule create_vk_object(const RenderContextVK* p_render_data) const;
};

/**
 * @class ShaderVK
 * @brief Shader class for Vulkan.
 *
 */
class WBE_META(WBE_RENDER_OBJECT) ROShaderVK final : public IRenderObjectVK {
public:
    /**
     * @brief Compatibility name for the resource creation description.
     */
    using Builder = ShaderDescVK;

    ROShaderVK() = delete;
    virtual ~ROShaderVK() override;
    ROShaderVK(const ROShaderVK&) = delete;
    ROShaderVK(ROShaderVK&&) = delete;
    ROShaderVK& operator=(const ROShaderVK&) = delete;
    ROShaderVK& operator=(ROShaderVK&&) = delete;

    virtual TypeID get_type_uuid() const override;

    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Constructor.
     *
     * @param p_render_context The base render data.
     * @param p_builder Configuration builder.
     */
    ROShaderVK(const RenderContextVK* p_render_context, const Builder& p_builder);
    /**
     * @brief Constructor from resource ID.
     *
     * @param p_render_context The base render data.
     * @param p_resource_id The resource ID.
     */
    ROShaderVK(const RenderContextVK* p_render_context, std::string_view p_resource_id);
    /**
     * @brief Constructor from a shader resource.
     *
     * @param p_render_context The base render data.
     * @param p_resource_id The shader's resource.
     */
    ROShaderVK(const RenderContextVK* p_render_context, RefRes<RUShader> p_resource);

    /**
     * @brief Get the shader module.
     *
     * @return The shader module.
     */
    VkShaderModule get_shader_module() const;

private:
    VkShaderModule shader_module = VK_NULL_HANDLE;

    void init(const Builder& p_builder);
};
} // namespace WhiteBirdEngine

#endif

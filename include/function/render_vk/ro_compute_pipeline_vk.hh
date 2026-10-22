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
#ifndef WBE_FILE_RO_COMPUTE_PIPELINE_VK_HH
#define WBE_FILE_RO_COMPUTE_PIPELINE_VK_HH
#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_pipeline_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "utils/utils.hh"
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

class ROComputePipelineVK;

/**
 * @struct ComputePipelineDescVK
 * @brief Resource creation description; copyable and movable.
 * Copying retains the shader reference and copies the entry name; moving transfers both.
 */
struct ComputePipelineDescVK {
    friend ROComputePipelineVK;
    friend ComputePipelineCompilerVK;
    /**
     * @class ShaderStage
     * @brief Compute shader stage configuration.
     */
    struct ShaderStage {
        /** The entry function name of the shader. */
        std::string entry_function = "main";
        /** Shader instance. */
        Ref<ROShaderVK> shader;
    };

    // Compute shader stage.
    Required<ShaderStage> shader_stage;

    /**
     * @brief Set the compute shader stage.
     *
     * @param p_shader The shader instance.
     * @param p_entry_function The shader entry function.
     * @return *this.
     */
    ComputePipelineDescVK& set_shader_stage(Ref<ROShaderVK> p_shader, std::string_view p_entry_function = "main") {
        shader_stage = ShaderStage{.entry_function = std::string(p_entry_function), .shader = p_shader};
        return *this;
    }

    /**
     * @brief Create a reference-counted compute pipeline object.
     *
     * @param p_render_context Renderer-owned resource creation context.
     * @return Reference-counted compute pipeline object.
     */
    Ref<ROComputePipelineVK> create_ref(const RenderContextVK* p_render_context) const;

    /**
     * @brief Create a unique-ownership compute pipeline object.
     *
     * @param p_render_context Renderer-owned resource creation context.
     * @return Unique-ownership compute pipeline object.
     */
    Unique<ROComputePipelineVK> create_unique(const RenderContextVK* p_render_context) const;
};

/**
 * @class ROComputePipelineVK
 * @brief Vulkan compute pipeline render object.
 */
class WBE_META(WBE_RENDER_OBJECT) ROComputePipelineVK final : public ROPipelineVK {
public:
    /**
     * @brief Compatibility name for the resource creation description.
     */
    using Builder = ComputePipelineDescVK;

    virtual ~ROComputePipelineVK();
    ROComputePipelineVK(const ROComputePipelineVK&) = delete;
    ROComputePipelineVK(ROComputePipelineVK&&) = delete;
    ROComputePipelineVK& operator=(const ROComputePipelineVK&) = delete;
    ROComputePipelineVK& operator=(ROComputePipelineVK&&) = delete;

    /**
     * @brief Construct a compute pipeline render object.
     *
     * @param p_render_data Base render data.
     * @param p_builder Builder containing the compute pipeline configuration.
     */
    ROComputePipelineVK(const RenderContextVK* p_render_data, const Builder& p_builder);

    virtual TypeID get_type_uuid() const override;

    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @class ExposedFields
     * @brief Exposed fields of ROComputePipelineVK.
     */
    struct ExposedFields {
        /**
         * @brief Compute shader used by this pipeline.
         */
        Ref<const ROShaderVK> compute_shader;
    };

    const ExposedFields& fields() const {
        return exposed_fields;
    }

private:
    ExposedFields exposed_fields;
};

} // namespace WhiteBirdEngine

#endif

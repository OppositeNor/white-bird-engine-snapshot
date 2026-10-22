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
#ifndef WBE_FILE_RO_RAY_TRACING_PIPELINE_VK_HH
#define WBE_FILE_RO_RAY_TRACING_PIPELINE_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/i_ro_ray_tracing_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <string_view>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

/**
 * @class RORayTracingPipelineVK
 * @brief The ray tracing pipeline.
 *
 */
class WBE_META(WBE_RENDER_OBJECT) RORayTracingPipelineVK : public IRORayTracingVK {
public:
    /**
     * @class Builder
     * @brief Builder for a Vulkan ray-tracing pipeline.
     *
     * The builder collects shader stages and Vulkan shader-group declarations, then passes them unchanged to the pipeline
     * construction path.
     */
    struct Builder {
        /**
         * @class ShaderStage
         * @brief One shader stage declared for the ray-tracing pipeline.
         */
        struct ShaderStage {
            /**
             * @brief The Vulkan stage represented by this shader.
             */
            VkShaderStageFlagBits stage;
            /**
             * @brief Entry-point function exported by the shader module.
             */
            std::string entry_function = "main";
            /**
             * @brief Shader module used for this stage.
             */
            Ref<ROShaderVK> shader;
        };

        enum class SBTRegion {
            RAYGEN,
            MISS,
            HIT,
            CALLABLE,
        };

        struct ShaderGroup {
            VkRayTracingShaderGroupCreateInfoKHR create_info;
            SBTRegion sbt_region;
        };

        /**
         * @brief Append one shader stage declaration.
         *
         * @param p_shader_stage Shader-stage description to copy into the builder.
         * @return *this.
         */
        Builder& add_shader_stage(const ShaderStage& p_shader_stage) {
            shader_stages.push_back(p_shader_stage);
            return *this;
        }

        /**
         * @brief Append one shader stage declaration.
         *
         * @param p_shader_stage Shader-stage description to move into the builder.
         * @return *this.
         */
        Builder& add_shader_stage(ShaderStage&& p_shader_stage) {
            shader_stages.push_back(std::move(p_shader_stage));
            return *this;
        }

        /**
         * @brief Start a new Vulkan shader-group declaration.
         *
         * The created group is initialized from SHADER_GROUP_CREATE_INFO_BASE and becomes the target of the subsequent
         * set_general_shader(), set_closest_hit(), set_any_hit(), and set_intersection_shader() calls.
         *
         * @param p_type Vulkan shader-group type to create.
         * @return *this.
         */
        Builder& new_shader_group(VkRayTracingShaderGroupTypeKHR p_type, SBTRegion p_region) {
            auto create_info = SHADER_GROUP_CREATE_INFO_BASE;
            create_info.type = p_type;
            shader_groups.push_back({.create_info = create_info, .sbt_region = p_region});
            return *this;
        }

        /**
         * @brief Set the general-shader stage index on the most recently added shader group.
         *
         * @param p_shader_stage_index Index into shader_stages.
         * @return *this.
         */
        Builder& set_general_shader(size_t p_shader_stage_index) {
            shader_groups.back().create_info.generalShader = p_shader_stage_index;
            return *this;
        }

        /**
         * @brief Set the closest-hit shader stage index on the most recently added shader group.
         *
         * @param p_shader_stage_index Index into shader_stages.
         * @return *this.
         */
        Builder& set_closest_hit(size_t p_shader_stage_index) {
            shader_groups.back().create_info.closestHitShader = p_shader_stage_index;
            return *this;
        }

        /**
         * @brief Set the any-hit shader stage index on the most recently added shader group.
         *
         * @param p_shader_stage_index Index into shader_stages.
         * @return *this.
         */
        Builder& set_any_hit(size_t p_shader_stage_index) {
            shader_groups.back().create_info.anyHitShader = p_shader_stage_index;
            return *this;
        }

        /**
         * @brief Set the intersection shader stage index on the most recently added shader group.
         *
         * @param p_shader_stage_index Index into shader_stages.
         * @return *this.
         */
        Builder& set_intersection_shader(size_t p_shader_stage_index) {
            shader_groups.back().create_info.intersectionShader = p_shader_stage_index;
            return *this;
        }

        // Next builder fields to add when this moves beyond the minimal prototype:
        // 1. Pipeline recursion depth.
        // 2. Optional library pipelines or library create infos.
        // 3. Pipeline create flags and interface data for payload / hit-attribute sizes.
        // 4. Optional per-region metadata if you want explicit SBT record ordering.

        /**
         * @brief Baseline Vulkan shader-group configuration used when creating a new group.
         *
         * Unused shader-stage indices default to VK_SHADER_UNUSED_KHR until explicitly assigned.
         */
        static constexpr VkRayTracingShaderGroupCreateInfoKHR SHADER_GROUP_CREATE_INFO_BASE = {
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .generalShader = VK_SHADER_UNUSED_KHR,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        };

        /**
         * @brief Vulkan shader-group declarations passed to pipeline creation.
         */
        std::vector<ShaderGroup> shader_groups;
        /**
         * @brief Shader stages referenced by the shader-group declarations.
         */
        std::vector<ShaderStage> shader_stages;
    };

    virtual ~RORayTracingPipelineVK();
    RORayTracingPipelineVK(const RORayTracingPipelineVK&) = delete;
    RORayTracingPipelineVK(RORayTracingPipelineVK&&) = delete;
    RORayTracingPipelineVK& operator=(const RORayTracingPipelineVK&) = delete;
    RORayTracingPipelineVK& operator=(RORayTracingPipelineVK&&) = delete;

    /**
     * @brief Construct a ray tracing pipeline render object.
     *
     * @param p_render_data Base render data.
     * @param p_builder Builder containing the ray tracing pipeline configuration.
     */
    RORayTracingPipelineVK(const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data, const Builder& p_builder);

    virtual TypeID get_type_uuid() const override;

    /**
     * @brief Name the pipeline, and register its shader binding table buffer as `<p_name>.shader_binding_table`.
     */
    virtual void apply_debug_name(std::string_view p_name) override;

private:
    size_t calculate_sbt_buffer_size(VkPipeline p_ray_pipeline,
        VkRayTracingPipelineCreateInfoKHR p_ray_pipeline_info,
        std::span<const VkRayTracingPipelineCreateInfoKHR> p_libraries_info);

    VkPipeline pipeline = VK_NULL_HANDLE;
    Vector<Ref<const ROShaderVK>, RenderAllocator> shaders;
    Ref<ROBufferVK> shader_binding_table_buffer = MEM_NULL;

    void create_sbt();
};

} // namespace WhiteBirdEngine

#endif

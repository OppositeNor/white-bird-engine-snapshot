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
#ifndef WBE_FILE_RO_GRAPHICS_PIPELINE_VK_HH
#define WBE_FILE_RO_GRAPHICS_PIPELINE_VK_HH

#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/mesh_node_contract.hh"
#include "function/render/vertex_input_requirement.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_pipeline_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

struct GraphicsPipelineDescVK;

/**
 * @class ROGraphicsPipelineVK
 * @brief The graphics pipeline.
 *
 */
class WBE_META(WBE_RENDER_OBJECT) ROGraphicsPipelineVK final : public ROPipelineVK {
public:
    /**
     * @class PipelineConfigurations
     * @brief Pipeline configurations.
     *
     */
    struct Config {
        // Shader configuration. Manual setup needed!
        struct ShaderConfig {
            // Shader stage configurations.
            struct ShaderStage {
                // The entry function name of the shader.
                std::string entry_function = "main";
                // Shader instance.
                Ref<ROShaderVK> shader;
            };
            // The vertex shader stage.
            Required<ShaderStage> vertex_shader;
            // The geometry shader stage.
            Required<ShaderStage> geometry_shader;
            // The fragment shader stage.
            Required<ShaderStage> fragment_shader;
            // TODO: TSC TSE shader setup

            /**
             * @brief Set vertex shader stage.
             *
             * @param p_shader The shader instance.
             * @return *this
             */
            ShaderConfig& set_vertex_shader(Ref<ROShaderVK> p_shader, std::string_view p_entry_function = "main") {
                vertex_shader = {.entry_function = std::string(p_entry_function), .shader = p_shader};
                return *this;
            }

            /**
             * @brief Set fragment shader stage.
             *
             * @param p_shader The shader instance.
             * @return *this
             */
            ShaderConfig& set_fragment_shader(Ref<ROShaderVK> p_shader, std::string_view p_entry_function = "main") {
                fragment_shader = {.entry_function = std::string(p_entry_function), .shader = p_shader};
                return *this;
            }

            /**
             * @brief Set geometry shader stage.
             *
             * @param p_shader The shader instance.
             * @return *this
             */
            ShaderConfig& set_geometry_shader(Ref<ROShaderVK> p_shader, std::string_view p_entry_function = "main") {
                geometry_shader = {.entry_function = std::string(p_entry_function), .shader = p_shader};
                return *this;
            }

            /**
             * @brief Set shaders.
             *
             * @param p_vertex_shader The vertex shader instance.
             * @param p_fragment_shader The fragment shader instance.
             * @param p_vertex_entry_function The vertex shader entry function.
             * @param p_fragment_entry_function The fragment shader entry
             * function.
             * @return *this.
             */
            ShaderConfig& set_shaders(Ref<ROShaderVK> p_vertex_shader,
                Ref<ROShaderVK> p_fragment_shader,
                std::string_view p_vertex_entry_function = "main",
                std::string_view p_fragment_entry_function = "main") {
                set_vertex_shader(p_vertex_shader, p_vertex_entry_function);
                set_fragment_shader(p_fragment_shader, p_fragment_entry_function);
                return *this;
            }
        };
        // Vertex input configurations.
        struct VertexInputConfig {
            // The vertex bindings.
            std::vector<VkVertexInputBindingDescription> bindings;
            // The vertex attributes.
            std::vector<VkVertexInputAttributeDescription> attributes;

            /**
             * @brief Set the vertex input bindings.
             *
             * @param p_bindings The vertex input bindings.
             * @return *this
             */
            VertexInputConfig& set_bindings(const std::vector<VkVertexInputBindingDescription>& p_bindings) {
                bindings = p_bindings;
                return *this;
            }

            /**
             * @brief Set the vertex input bindings.
             *
             * @param p_bindings The vertex input bindings.
             * @return *this
             */
            VertexInputConfig& set_bindings(std::vector<VkVertexInputBindingDescription>&& p_bindings) {
                bindings = std::move(p_bindings);
                return *this;
            }

            /**
             * @brief Set the vertex input attributes.
             *
             * @param p_attributes The vertex input attributes.
             * @return *this
             */
            VertexInputConfig& set_attributes(const std::vector<VkVertexInputAttributeDescription>& p_attributes) {
                attributes = p_attributes;
                return *this;
            }

            /**
             * @brief Set the vertex input attributes.
             *
             * @param p_attributes The vertex input attributes.
             * @return *this
             */
            VertexInputConfig& set_attributes(std::vector<VkVertexInputAttributeDescription>&& p_attributes) {
                attributes = std::move(p_attributes);
                return *this;
            }

            /**
             * @brief Add a binding to the vertex input bindings.
             *
             * @param p_binding The binding to add to the vertex input bindings.
             * @return *this
             */
            VertexInputConfig& add_binding(VkVertexInputBindingDescription p_binding) {
                bindings.push_back(p_binding);
                return *this;
            }

            /**
             * @brief Add a range of bindings to the vertex input bindings.
             *
             * @param p_bindings The bindings to add to the vertex input
             * bindings.
             * @return *this
             */
            VertexInputConfig& add_bindings(std::span<const VkVertexInputBindingDescription> p_bindings) {
                bindings.append_range(p_bindings);
                return *this;
            }

            /**
             * @brief Add an atribute to the vertex input attributes.
             *
             * @param p_attribute The attribute to add to the vertex input
             * attributes.
             * @return *this
             */
            VertexInputConfig& add_attribute(VkVertexInputAttributeDescription p_attribute) {
                attributes.push_back(p_attribute);
                return *this;
            }

            /**
             * @brief Add a range of atributes to the vertex input attributes.
             *
             * @param p_attributes The attributes to add to the vertex input
             * attributes.
             * @return *this
             */
            VertexInputConfig& add_attributes(std::span<const VkVertexInputAttributeDescription> p_attributes) {
                attributes.append_range(p_attributes);
                return *this;
            }
        };
        // Input assembly configurations.
        struct InputAssemblyConfig {
            // The topology used in the input assembly stage.
            VkPrimitiveTopology primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            // Enable restart for strip and fan primitive topology.
            VkBool32 primitive_restart_enabled = VK_FALSE;

            /**
             * @brief Set the primitive input topology.
             *
             * @param p_primitive_topology The primitive input topology.
             * @return *this
             */
            InputAssemblyConfig& set_primitive_topology(VkPrimitiveTopology p_primitive_topology) {
                primitive_topology = p_primitive_topology;
                return *this;
            }

            /**
             * @brief Enable primitive restart.
             *
             * @return *this
             */
            InputAssemblyConfig& enable_primitive_restart() {
                primitive_restart_enabled = VK_TRUE;
                return *this;
            }

            /**
             * @brief Disable primitive restart.
             *
             * @return *this
             */
            InputAssemblyConfig& disable_primitive_restart() {
                primitive_restart_enabled = VK_FALSE;
                return *this;
            }

            /**
             * @brief Set primitive restart.
             *
             * @param p_primitive_restart_enabled VK_TRUE to enable primitive
             * restart, VK_FALSE otherwise.
             * @return *this
             */
            InputAssemblyConfig& set_primitive_restart(VkBool32 p_primitive_restart_enabled) {
                primitive_restart_enabled = p_primitive_restart_enabled;
                return *this;
            }
        };
        // Teselation configurations.
        struct TesselationConfig {
            // TODO
        };
        // Viewport configuration.
        struct ViewportStateConfig {
            // Viewports. Manual setup needed!
            std::vector<VkViewport> viewports;
            // Scissors. Manual setup needed!
            // A scissor clamps a rectangle region that the fragments will only
            // be generated inside this region.
            std::vector<VkRect2D> scissors;

            /**
             * @brief Add a viewport.
             *
             * @param p_viewport The viewport to add.
             * @param p_scissor The scissor of this viewport.
             * @return *this
             */
            ViewportStateConfig& add_viewport(VkViewport p_viewport, VkRect2D p_scissor) {
                viewports.push_back(p_viewport);
                scissors.push_back(p_scissor);
                return *this;
            }

            /**
             * @brief Add a viewport.
             *
             * @param p_viewport The viewport to add.
             * @return *this
             */
            ViewportStateConfig& add_viewport(VkViewport p_viewport) {
                VkRect2D scissor{};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = static_cast<uint32_t>(p_viewport.width);
                scissor.extent.height = static_cast<uint32_t>(p_viewport.height);
                viewports.push_back(p_viewport);
                scissors.push_back(scissor);
                return *this;
            }
        };
        // Rasterization configuraion.
        struct RasterizationConfig {
            // If set to true, the fragments of depth over 1.0 or smaller than
            // 0.0 won't be discarded but will be clamped.
            VkBool32 depth_clamp_enabled = VK_FALSE;
            // Polygon mode.
            VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
            // Cull mode. Default cull back faces.
            VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
            // Which direction is considered front, default to
            // counter-clockwise.
            VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            /**
             * @brief Whether rasterization overestimates primitive coverage.
             */
            bool conservative_rasterization = false;

            /**
             * @brief Enalbe depth clamp.
             *
             * @return *this
             */
            RasterizationConfig& enable_depth_clamp() {
                depth_clamp_enabled = VK_TRUE;
                return *this;
            }

            /**
             * @brief Disable depth clamp.
             *
             * @return *this
             */
            RasterizationConfig& disable_depth_clamp() {
                depth_clamp_enabled = VK_FALSE;
                return *this;
            }

            /**
             * @brief Enable/disable depth clamp.
             *
             * @param p_depth_clamp_enabled VK_TRUE to enable depth clamp,
             * VK_FALSE otherwise.
             * @return
             */
            RasterizationConfig& set_depth_clamp(bool p_depth_clamp_enabled) {
                depth_clamp_enabled = p_depth_clamp_enabled ? VK_TRUE : VK_FALSE;
                return *this;
            }

            /**
             * @brief Set polygon mode.
             *
             * @param p_polygon_mode The polygon mode to set to.
             * @return *this
             */
            RasterizationConfig& set_polygon_mode(VkPolygonMode p_polygon_mode) {
                polygon_mode = p_polygon_mode;
                return *this;
            }

            /**
             * @brief Set cull mode.
             *
             * @param p_cull_mode The cull mode to set to.
             * @return *this
             */
            RasterizationConfig& set_cull_mode(VkCullModeFlags p_cull_mode) {
                cull_mode = p_cull_mode;
                return *this;
            }

            /**
             * @brief Set front face.
             *
             * @param p_front_face The front face to set to.
             * @return *this
             */
            RasterizationConfig& set_front_face(VkFrontFace p_front_face) {
                front_face = p_front_face;
                return *this;
            }

            /**
             * @brief Enable or disable conservative overestimation rasterization.
             * @param p_conservative_rasterization Whether conservative rasterization is enabled.
             * @return This rasterization configuration.
             */
            RasterizationConfig& set_conservative_rasterization(bool p_conservative_rasterization) {
                conservative_rasterization = p_conservative_rasterization;
                return *this;
            }
        };
        // Multisample configurations.
        struct MultisampleConfig {
            // TODO
        };
        // Depth test configurations.
        struct DepthTestConfig {
            // Depth test enabled.
            VkBool32 depth_test_enabled = VK_TRUE;
            // Enable depth writing.
            VkBool32 depth_write_enabled = VK_TRUE;
            // Compare operation.
            VkCompareOp depth_compare_op = VK_COMPARE_OP_GREATER;

            /**
             * @brief Enalbe depth test.
             *
             * @return *this
             */
            DepthTestConfig& enable_depth_test() {
                depth_test_enabled = VK_TRUE;
                return *this;
            }

            /**
             * @brief Disable depth test.
             *
             * @return *this
             */
            DepthTestConfig& disable_depth_test() {
                depth_test_enabled = VK_FALSE;
                return *this;
            }

            /**
             * @brief Enable/disable depth test.
             *
             * @param p_depth_test_enabled VK_TRUE to enable depth test,
             * VK_FALSE otherwise.
             * @return
             */
            DepthTestConfig& set_depth_test(VkBool32 p_depth_test_enabled) {
                depth_test_enabled = p_depth_test_enabled;
                return *this;
            }

            /**
             * @brief Enalbe depth write.
             *
             * @return *this
             */
            DepthTestConfig& enable_depth_write() {
                depth_write_enabled = VK_TRUE;
                return *this;
            }

            /**
             * @brief Disable depth write.
             *
             * @return *this
             */
            DepthTestConfig& disable_depth_write() {
                depth_write_enabled = VK_FALSE;
                return *this;
            }

            /**
             * @brief Enable/disable depth write.
             *
             * @param p_depth_write_enabled VK_TRUE to enable depth write,
             * VK_FALSE otherwise.
             * @return
             */
            DepthTestConfig& set_depth_write(VkBool32 p_depth_write_enabled) {
                depth_write_enabled = p_depth_write_enabled;
                return *this;
            }

            /**
             * @brief Set depth compare operation.
             *
             * @param p_compare_op The compare operation to set to.
             * @return *this.
             */
            DepthTestConfig& set_depth_compare_op(VkCompareOp p_compare_op) {
                depth_compare_op = p_compare_op;
                return *this;
            }
        };
        // Color blend configurations.
        struct ColorBlendConfig {
            // Enable logic operations for the attachments.
            VkBool32 logic_op_enabled = VK_FALSE;
            // Logic operation. Ignored if logic_op_enable is VK_FALSE.
            VkLogicOp logic_op;
            // Color blend attachments.
            std::vector<VkPipelineColorBlendAttachmentState> attachments;

            /**
             * @brief Enaable logic operation.
             *
             * @param p_logic_op The logic operation to set to.
             * @return *this
             */
            ColorBlendConfig& enalbe_logic_op(VkLogicOp p_logic_op) {
                logic_op_enabled = VK_TRUE;
                logic_op = p_logic_op;
                return *this;
            }

            /**
             * @brief Disable logic operation.
             *
             * @return *this
             */
            ColorBlendConfig& disable_logic_op() {
                logic_op_enabled = VK_FALSE;
                return *this;
            }

            /**
             * @brief Add a default color attachment.
             *
             * @return *this
             */
            ColorBlendConfig& add_default_color_attachment() {
                attachments.push_back({.blendEnable = VK_FALSE,
                    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT});
                return *this;
            }
        };
        // Dynamic state configurations.
        struct DynamicStateConfig {
            // Dynamic states.
            std::vector<VkDynamicState> dynamic_states;

            DynamicStateConfig& add_default_dynamic_states() {
                dynamic_states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
                dynamic_states.push_back(VK_DYNAMIC_STATE_SCISSOR);
                return *this;
            }

            /**
             * @brief Add a dynamic state.
             *
             * @param p_dynamic_state The dynamic state to be added.
             * @return *this
             */
            DynamicStateConfig& add_dynamic_state(VkDynamicState p_dynamic_state) {
                dynamic_states.push_back(p_dynamic_state);
                return *this;
            }

            /**
             * @brief Set the dynamic states.
             *
             * @param p_dynamic_state The dynamic states to set to.
             * @return *this
             */
            DynamicStateConfig& set_dynamic_states(const std::vector<VkDynamicState>& p_dynamic_state) {
                dynamic_states = p_dynamic_state;
                return *this;
            }

            /**
             * @brief Set the dynamic states.
             *
             * @param p_dynamic_state The dynamic states to set to.
             * @return *this
             */
            DynamicStateConfig& set_dynamic_states(std::vector<VkDynamicState>&& p_dynamic_state) {
                dynamic_states = std::move(p_dynamic_state);
                return *this;
            }
        };

        /**
         * @brief Configurations for attachments.
         */
        struct AttachmentConfig {
            // Color attachments' formats.
            std::vector<VkFormat> color_formats;
            // Depth attachment's format.
            Required<VkFormat> depth_formats;
            // Stencil attachment's format.
            Required<VkFormat> stencil_formats;
            /**
             * @brief Add a color format.
             *
             * @param p_format The format to add.
             * @return *this
             */
            AttachmentConfig& add_color_format(VkFormat p_format) {
                color_formats.push_back(p_format);
                return *this;
            }

            /**
             * @brief Add color formats.
             *
             * @param p_formats The formats to add.
             * @return *this
             */
            AttachmentConfig& add_color_formats(std::span<const VkFormat> p_formats) {
                color_formats.append_range(p_formats);
                return *this;
            }

            /**
             * @brief Set the color formats.
             *
             * @param p_formats The formats to set to.
             * @return *this
             */
            AttachmentConfig& set_color_formats(const std::vector<VkFormat>& p_formats) {
                color_formats = p_formats;
                return *this;
            }

            /**
             * @brief Set the color formats.
             *
             * @param p_formats The formats to set to.
             * @return *this
             */
            AttachmentConfig& set_color_formats(std::vector<VkFormat>&& p_formats) {
                color_formats = std::move(p_formats);
                return *this;
            }

            /**
             * @brief Set the depth format.
             *
             * @param p_format The format to set to.
             * @return *this
             */
            AttachmentConfig& set_depth_format(VkFormat p_format) {
                depth_formats = p_format;
                return *this;
            }

            /**
             * @brief Set the stencil format.
             *
             * @param p_format The format to set to.
             * @return *this
             */
            AttachmentConfig& set_stencil_format(VkFormat p_format) {
                stencil_formats = p_format;
                return *this;
            }
        };

        // The input shaders. Manual setup needed!
        ShaderConfig shader{};
        // Vertex input stage configurations.
        VertexInputConfig vertex_input{};
        // Input assembly stage configurations.
        InputAssemblyConfig input_assembly{};
        // Teselation stage configurations.
        std::optional<TesselationConfig> tesselation;
        // Viewport state configuration.
        ViewportStateConfig viewport{};
        // Rasterization stage configurations.
        RasterizationConfig rasterization{};
        // Multisample state configurations.
        MultisampleConfig multisample{};
        // Depth test configuration.
        DepthTestConfig depth_test{};
        // Color blend configuration.
        ColorBlendConfig color_blend{};
        // Dynamic state configuration.
        DynamicStateConfig dynamic_state{};
        // Attachment configuration.
        AttachmentConfig attachment_config{};
        // Indicate which subpass this graphics pipeline uses.
        uint32_t subpass_index = 0;
    };

    /**
     * @brief Compatibility name for the resource creation description.
     */
    using Builder = GraphicsPipelineDescVK;

    ~ROGraphicsPipelineVK();
    ROGraphicsPipelineVK(const ROGraphicsPipelineVK&) = delete;
    ROGraphicsPipelineVK(ROGraphicsPipelineVK&&) = delete;
    ROGraphicsPipelineVK& operator=(const ROGraphicsPipelineVK&) = delete;
    ROGraphicsPipelineVK& operator=(ROGraphicsPipelineVK&&) = delete;

    virtual TypeID get_type_uuid() const override;

    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Constructor.
     *
     * @param p_builder The builder instance.
     */
    ROGraphicsPipelineVK(const RenderContextVK* p_render_data, const Builder& p_builder);

    /**
     * @brief Get the configurations of the graphics pipeline.
     *
     * @return The configurations of the graphics pipeline.
     */
    const Config& get_config() const {
        return config;
    }

    struct ExposedFields {
        Ref<const ROShaderVK> vertex_shader;
        Ref<const ROShaderVK> geometry_shader;
        Ref<const ROShaderVK> fragment_shader;
        // TODO: TSC TSE shaders
    };

    const ExposedFields& fields() const {
        return exposed_fields;
    }

    /**
     * @brief Get the vertex input the pipeline's vertex entry consumes.
     *
     * Set by GraphicsPipelineCompilerVK from the vertex shader's reflection; empty for a vertex-less entry and for a pipeline
     * built without the compiler. Match it against a submesh's vertex bindings with build_vertex_input_description_vk() to draw
     * the submesh; the pipeline uses dynamic vertex input state, so one pipeline draws any matching layout.
     */
    const VertexInputRequirement& get_vertex_input_requirement() const {
        return vertex_input_requirement;
    }

    /**
     * @brief Get the material texture contract the pipeline's shaders declare.
     *
     * Set by GraphicsPipelineCompilerVK; std::nullopt when the shaders declare no material tables, and for a pipeline built
     * without the compiler. A mesh drawn with the pipeline builds its material parameter table for this contract; see
     * MeshResourcesVK::acquire_material_table().
     */
    const std::optional<MaterialTextureContract>& get_material_texture_contract() const {
        return material_texture_contract;
    }

    /**
     * @brief Get the mesh node layout the pipeline's vertex shader declares in its `mesh` parameter block.
     *
     * Set by GraphicsPipelineCompilerVK; std::nullopt when the vertex shader declares no `mesh` parameter block, and for a
     * pipeline built without the compiler. See MeshResourcesVK::validate_node_contract().
     */
    const std::optional<MeshNodeContract>& get_mesh_node_contract() const {
        return mesh_node_contract;
    }

private:
    friend class GraphicsPipelineCompilerVK;

    Config config;
    ExposedFields exposed_fields;
    VertexInputRequirement vertex_input_requirement;
    std::optional<MaterialTextureContract> material_texture_contract;
    std::optional<MeshNodeContract> mesh_node_contract;

    struct PipelineStateCreateInfo {
        VkPipelineViewportStateCreateInfo viewport_state{};
        VkPipelineVertexInputStateCreateInfo vertex_input_state{};
        VkPipelineInputAssemblyStateCreateInfo input_assembly_state{};
        Required<VkPipelineTessellationStateCreateInfo> tessellation_state;
        VkPipelineRasterizationStateCreateInfo rasterization_state{};
        VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_rasterization_state{};
        VkPipelineMultisampleStateCreateInfo multisample_state{};
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
        VkPipelineColorBlendStateCreateInfo color_blend_state{};
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        VkPipelineRenderingCreateInfo pipeline_rendering{};
    };

    void init(const Config& p_config);
    void cache_exposed_shader_fields(const Config& p_config);
    PipelineStateCreateInfo make_pipeline_state_create_info(const Config& p_config);
    VkGraphicsPipelineCreateInfo make_graphics_pipeline_create_info(const Config& p_config,
        const std::vector<VkPipelineShaderStageCreateInfo>& p_shader_stages,
        PipelineStateCreateInfo& p_state_create_info,
        VkPipelineCreateFlags2CreateInfo& p_create_flags) const;
    VkPipeline create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& p_create_info) const;

    void setup_vertex_input_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_input_assembly_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_tessellation_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_viewport_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_rasterization_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_mulitsample_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_depth_stencil_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_color_blend_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_dynamic_state_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);
    void setup_pipeline_rendering_create_info(const Config& p_config, PipelineStateCreateInfo& p_pipeline_create_infos);

    static std::vector<VkPipelineShaderStageCreateInfo> gather_shader_stages(const Config& p_config);
    static VkPipelineShaderStageCreateInfo get_shader_stage_create_info(
        VkShaderStageFlagBits p_stage_flag, const Config::ShaderConfig::ShaderStage& p_stage);
};

/**
 * @struct GraphicsPipelineDescVK
 * @brief Resource creation description; copyable and movable.
 * Copying duplicates configuration containers and retains shader references; moving transfers the configuration.
 */
struct GraphicsPipelineDescVK {
    /**
     * @brief Graphics pipeline configuration type.
     */
    using Config = ROGraphicsPipelineVK::Config;
    friend ROGraphicsPipelineVK;
    friend GraphicsPipelineCompilerVK;

    // Graphics pipeline configuration payload.
    Config config{};

    /**
     * @brief Set the full graphics pipeline config by copy.
     *
     * @param p_config The configuration to copy into this builder.
     * @return *this.
     */
    GraphicsPipelineDescVK& set_config(const Config& p_config) {
        config = p_config;
        return *this;
    }

    /**
     * @brief Set the full graphics pipeline config by move.
     *
     * @param p_config The configuration to move into this builder.
     * @return *this.
     */
    GraphicsPipelineDescVK& set_config(Config&& p_config) {
        config = std::move(p_config);
        return *this;
    }

    /**
     * @brief Apply default opaque-rendering pipeline configuration.
     *
     * This helper configures empty static vertex input, viewport/scissor
     * from render data, undefined depth/stencil formats, and dynamic vertex
     * input state. Color formats and blend attachments must be added
     * explicitly by the caller.
     *
     * @param p_render_data Base render data for viewport/scissor defaults.
     * @param p_shader_config Shader stage configuration.
     * @return *this.
     */
    GraphicsPipelineDescVK& default_render_opaque_configuration(const RenderContextVK* p_render_data, const Config::ShaderConfig& p_shader_config) {

        config.shader = p_shader_config;
        config.vertex_input = {};
        config.viewport.viewports = {p_render_data->viewport};
        config.viewport.scissors = {p_render_data->scissor};
        config.attachment_config.set_depth_format(VK_FORMAT_UNDEFINED).set_stencil_format(VK_FORMAT_UNDEFINED);
        config.dynamic_state.add_default_dynamic_states().add_dynamic_state(VK_DYNAMIC_STATE_VERTEX_INPUT_EXT);
        return *this;
    }

    /**
     * @brief Create a reference-counted graphics pipeline object.
     *
     * @param p_render_context Renderer-owned resource creation context.
     * @return Reference-counted graphics pipeline object.
     */
    Ref<ROGraphicsPipelineVK> create_ref(const RenderContextVK* p_render_context) const;

    /**
     * @brief Create a unique-ownership graphics pipeline object.
     *
     * @param p_render_context Renderer-owned resource creation context.
     * @return Unique-ownership graphics pipeline object.
     */
    Unique<ROGraphicsPipelineVK> create_unique(const RenderContextVK* p_render_context) const;
};

} // namespace WhiteBirdEngine

#endif

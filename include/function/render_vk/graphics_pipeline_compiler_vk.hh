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
#ifndef WBE_FILE_GRAPHICS_PIPELINE_COMPILER_VK_HH
#define WBE_FILE_GRAPHICS_PIPELINE_COMPILER_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/pipeline_compiler_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/glm.hpp>
#include <string_view>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class RendererVK;

/**
 * @class GraphicsPipelineCompilerVKContext
 * @brief Runtime state required to compile a graphics pipeline resource.
 */
struct GraphicsPipelineCompilerVKContext {
    /**
     * @brief Allocator used for generated render graph descriptors.
     */
    RenderAllocator* allocator = nullptr;
    /**
     * @brief Vulkan renderer state used by render object builders.
     */
    const RenderContextVK* render_data = nullptr;
    /**
     * @brief Registry used to reuse compiled render objects by resource key.
     */
    Ref<RenderDatabase> render_database = MEM_NULL;
    /**
     * @brief Renderer used to execute the generated task's render graph.
     */
    RendererVK* renderer = nullptr;
    /**
     * @brief Dynamic-rendering color attachment formats.
     */
    std::vector<VkFormat> color_attachment_formats;
    /**
     * @brief Dynamic-rendering depth attachment format.
     */
    VkFormat depth_attachment_format = VK_FORMAT_UNDEFINED;
    /**
     * @brief Dynamic-rendering stencil attachment format.
     */
    VkFormat stencil_attachment_format = VK_FORMAT_UNDEFINED;
    /**
     * @brief Polygon rasterization mode used by the compiled pipeline.
     */
    VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL;
    /**
     * @brief Primitive topology the compiled pipeline assembles its vertices into.
     */
    VkPrimitiveTopology primitive_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    /**
     * @brief Name of the global shader variable holding the material texture table; see MaterialTextureContractResolver.
     */
    std::string material_textures_variable_name = "material_textures";
    /**
     * @brief Name of the global shader variable holding the material parameter table; see MaterialTextureContractResolver.
     */
    std::string material_params_variable_name = "material_params";
};

/**
 * @class GraphicsPipelineCompilerVK
 * @brief Compiles a graphics pipeline resource unit into render graph objects.
 *
 * The compiler reuses registered shaders, textures, and graphics pipelines
 * when matching entries already exist in the render database.
 */
class GraphicsPipelineCompilerVK : public PipelineCompilerVK {
public:
    struct CompileResult {
        Ref<ROGraphicsPipelineVK> graphics_pipeline = MEM_NULL;
    };

    /**
     * @brief Construct a compiler with the given runtime context.
     *
     * @param p_context Runtime compilation context.
     */
    explicit GraphicsPipelineCompilerVK(const GraphicsPipelineCompilerVKContext& p_context);

    /**
     * @brief Compile a graphics pipeline resource into Vulkan render objects and reflected descriptor binding locations.
     *
     * @param p_resource_key Key of the graphics pipeline resource.
     * @param p_pipeline_resource Loaded graphics pipeline resource unit.
     * @return Compiled immutable pipeline state and its reflected descriptor layout.
     */
    CompileResult compile(std::string_view p_resource_key, RefRes<RUGraphicsPipeline> p_pipeline_resource);

    /**
     * @brief Compile a texture resource view for descriptor-heap use.
     *
     * @param p_resource_key Cache key for the texture render objects: the image is registered under it and the view under
     * `<p_resource_key>#image_view`, which also name them.
     * @param p_texture_resource Texture resource to upload.
     * @return The cached or newly compiled image view.
     */
    Ref<ROImageViewVK> compile_texture_view(std::string_view p_resource_key, RefRes<RUTexture> p_texture_resource);

private:
    GraphicsPipelineCompilerVKContext context;

    Ref<ROGraphicsPipelineVK> compile_graphics_pipeline(std::string_view p_resource_key,
        const RUGraphicsPipeline& p_pipeline_resource,
        Ref<ROShaderVK> p_vertex_shader,
        Ref<ROShaderVK> p_geometry_shader,
        Ref<ROShaderVK> p_fragment_shader,
        std::string_view p_vertex_stage_entry,
        std::string_view p_geometry_stage_entry,
        std::string_view p_fragment_stage_entry);
    Ref<ROImageVK> compile_texture(std::string_view p_resource_key, RefRes<RUTexture> p_texture_resource);
};

} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_RT_PARAMS_GRAPHICS_DRAW_MESH_VK_HH
#define WBE_FILE_RT_PARAMS_GRAPHICS_DRAW_MESH_VK_HH

#include "core/data/data_value.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "function/render_vk/rend_extent_2d_params_vk.hh"
#include "function/render_vk/rt_params_clear_value_vk.hh"
#include "function/render_vk/vulkan_enums_vk.hh"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RTParamsGraphicsDrawMeshVK
 * @brief Recipe parameters of RTGraphicsDrawMeshVK. Copyable and movable.
 *
 * Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsGraphicsDrawMeshVK {
    /**
     * @brief Resource key of the compiled mesh to draw. The compiled mesh is imported into the graph under this key.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(database, MeshResourcesVK))
    std::string mesh;
    /**
     * @brief Map from material pipeline variants to the resource keys of the compiled graphics pipelines in the renderer's
     * RenderDatabase that draw them; materials without a pipeline_variant use MATERIAL_PIPELINE_VARIANT_DEFAULT.
     *
     * The map needs at least one entry. Every pipeline must render to the same targets, and the pipelines that read the
     * material table must share one material texture contract. Drawing a material whose variant has no pipeline throws; a
     * variant no material uses is allowed. make_default_pipeline() builds the map of a task that draws every material with
     * one pipeline.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(database, ROGraphicsPipelineVK))
    DataValue pipeline;
    /**
     * @brief Parts of the mesh the task draws, in draw order; at least one, each at most once.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<MeshPartVK> parts;
    /**
     * @brief Graph resource id of the whole-mesh transform buffer (one GeometryNodeGPU); required when the pipeline declares
     * REND_RES_MESH_TRANSFORM.
     */
    WBE_META(WBE_REFLECT, WBE_RECIPE_RESOURCE(graph))
    std::optional<std::string> transform;
    /**
     * @brief Render area; the swap chain extent by default.
     */
    WBE_META(WBE_REFLECT)
    RendExtent2DParamsVK render_extent;
    /**
     * @brief Clear values of the render targets, colors in index order followed by depth.
     */
    WBE_META(WBE_REFLECT)
    std::vector<RTParamsClearValueVK> clear_values;
    /**
     * @brief Load operation of the color render targets.
     */
    WBE_META(WBE_REFLECT)
    AttachmentLoadOpVK color_load_op = AttachmentLoadOpVK::CLEAR;
    /**
     * @brief Load operation of the depth render target.
     */
    WBE_META(WBE_REFLECT)
    AttachmentLoadOpVK depth_load_op = AttachmentLoadOpVK::CLEAR;

    /**
     * @brief Make a pipeline map that draws the default pipeline variant with one pipeline.
     * @param p_pipeline_key Resource key of the compiled graphics pipeline.
     * @return The map { MATERIAL_PIPELINE_VARIANT_DEFAULT = p_pipeline_key }.
     */
    static DataValue make_default_pipeline(std::string_view p_pipeline_key) {
        DataValue result = DataValue::make_map();
        result.set_value(MATERIAL_PIPELINE_VARIANT_DEFAULT, std::string(p_pipeline_key));
        return result;
    }
};

} // namespace WhiteBirdEngine

#endif

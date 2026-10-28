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
#ifndef WBE_FILE_SVO_RECIPE_PARAMS_HH
#define WBE_FILE_SVO_RECIPE_PARAMS_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render_vk/rend_graph_recipe_params_vk.hh"
#include "recipe_feature_params.hh"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @enum SVORecipeVariant
 * @brief The SVO construction a recipe builds: atomic publication or pass publication.
 */
enum class WBE_META(WBE_REFLECT_SCRIPT) SVORecipeVariant : uint8_t {
    /** Construction publishes nodes with atomics as it allocates them. */
    ATOMIC WBE_META(WBE_SERIALIZATION_ALIASES(atomic)),
    /** Separate passes publish the nodes each construction pass allocates. */
    PASS_PUBLISHED WBE_META(WBE_SERIALIZATION_ALIASES(pass_published)),
};

/**
 * @class SVOBufferSizes
 * @brief The byte sizes of the SVO scratch buffers the recipe declares. Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) SVOBufferSizes {
    /**
     * @brief Each of svo_deferred_list_ping and svo_deferred_list_pong.
     */
    WBE_META(WBE_REFLECT)
    uint64_t deferred_list = 0;
    /**
     * @brief svo_pre_deferred_debug_info_buffer.
     */
    WBE_META(WBE_REFLECT)
    uint64_t pre_deferred_debug_info = 0;
    /**
     * @brief svo_pre_deferred_debug_list_buffer.
     */
    WBE_META(WBE_REFLECT)
    uint64_t pre_deferred_debug_list = 0;
};

/**
 * @class SVORecipeParams
 * @brief The parameters of the render_graph_utils.features.voxel_svo recipe module; see ISVORenderFeature::configure_recipe().
 * @details Copyable and movable.
 */
struct WBE_META(WBE_REFLECT_SCRIPT) SVORecipeParams : public RecipeFeatureParams {
    /**
     * @brief The construction the recipe builds.
     */
    WBE_META(WBE_REFLECT)
    SVORecipeVariant variant = SVORecipeVariant::ATOMIC;
    /**
     * @brief Resource-key prefix of the static construction pipelines.
     */
    WBE_META(WBE_REFLECT)
    std::string shader_prefix;
    /**
     * @brief Resource-key prefix of the dynamic update pipelines.
     */
    WBE_META(WBE_REFLECT)
    std::string dynamic_shader_prefix;
    /**
     * @brief The import names of the application's SVO buffers.
     */
    WBE_META(WBE_REFLECT)
    std::vector<std::string> imports;
    /**
     * @brief The byte sizes of the scratch buffers.
     */
    WBE_META(WBE_REFLECT)
    SVOBufferSizes buffer_sizes;
    /**
     * @brief The rasterized resolution of the construction passes.
     */
    WBE_META(WBE_REFLECT)
    uint32_t extent = 0;
    /**
     * @brief The tree depth, which is the number of deferred propagation rounds.
     */
    WBE_META(WBE_REFLECT)
    uint32_t max_depth = 0;
    /**
     * @brief Workgroups of the static reset.
     */
    WBE_META(WBE_REFLECT)
    uint32_t reset_group_count = 0;
    /**
     * @brief Workgroups of each deferred propagation round.
     */
    WBE_META(WBE_REFLECT)
    uint32_t deferred_dispatch_group_count = 0;
    /**
     * @brief Workgroups of the dynamic clear.
     */
    WBE_META(WBE_REFLECT)
    uint32_t dynamic_clear_group_count = 0;
    /**
     * @brief Workgroups of the static publication.
     */
    WBE_META(WBE_REFLECT)
    uint32_t static_publication_group_count = 0;
    /**
     * @brief Workgroups of the dynamic publication.
     */
    WBE_META(WBE_REFLECT)
    uint32_t dynamic_publication_group_count = 0;
    /**
     * @brief The trigger of the static construction tasks, which record once; unset to rebuild them every frame.
     */
    WBE_META(WBE_REFLECT)
    std::optional<RendRecipeTriggerVK> static_build;
};

} // namespace WhiteBirdEngine

#endif

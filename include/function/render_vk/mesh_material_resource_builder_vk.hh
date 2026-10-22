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
#ifndef WBE_FILE_MESH_MATERIAL_RESOURCE_BUILDER_VK_HH
#define WBE_FILE_MESH_MATERIAL_RESOURCE_BUILDER_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/render_resource_unit.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @struct MeshMaterialResourcesVK
 * @brief Packed material-table data and resources retained by one mesh task.
 */
struct MeshMaterialResourcesVK {
    std::vector<uint8_t> material_params_data;
    std::vector<RefRes<RUTexture>> material_textures;
    Ref<IRenderObject> material_sampler = MEM_NULL;
};

/**
 * @class MeshMaterialResourceBuilderVK
 * @brief Builds mesh material parameter data and retained resources for one graphics pipeline.
 */
class MeshMaterialResourceBuilderVK final {
public:
    using TextureDescriptorIndexResolver = std::function<uint32_t(RefRes<RUTexture>)>;

    /**
     * @brief Construct a stateless builder.
     */
    MeshMaterialResourceBuilderVK() = default;
    /**
     * @brief Destroy a stateless builder.
     */
    ~MeshMaterialResourceBuilderVK() = default;
    /**
     * @brief Copy a stateless builder.
     */
    MeshMaterialResourceBuilderVK(const MeshMaterialResourceBuilderVK&) = default;
    /**
     * @brief Move a stateless builder.
     */
    MeshMaterialResourceBuilderVK(MeshMaterialResourceBuilderVK&&) noexcept = default;
    /**
     * @brief Copy a stateless builder.
     */
    MeshMaterialResourceBuilderVK& operator=(const MeshMaterialResourceBuilderVK&) = default;
    /**
     * @brief Move a stateless builder.
     */
    MeshMaterialResourceBuilderVK& operator=(MeshMaterialResourceBuilderVK&&) noexcept = default;

    /**
     * @brief Build resources for one reflected material contract.
     *
     * Returns empty resources when p_material_contract is std::nullopt.
     *
     * @param p_material_contract Reflected material contract for the selected pipeline.
     * @param p_materials Ordered deduplicated material rows referenced by static submeshes.
     * @param p_default_material Default material supplying fallback texture roles.
     * @param p_max_texture_descriptor_count Maximum sampled-image descriptors supported for the runtime array.
     * @param p_material_sampler Optional sampler retained by the task.
     * @param p_texture_descriptor_index_resolver Optional resolver that replaces table-local texture indices with resource-heap indices.
     * @throws std::runtime_error When reflected limits are exceeded or material role data is malformed.
     */
    MeshMaterialResourcesVK build(const std::optional<MaterialTextureContract>& p_material_contract,
        std::span<const RefRes<RUMaterial>> p_materials,
        RefRes<RUMaterial> p_default_material,
        uint32_t p_max_texture_descriptor_count,
        Ref<IRenderObject> p_material_sampler = MEM_NULL,
        const TextureDescriptorIndexResolver& p_texture_descriptor_index_resolver = {}) const;
};

} // namespace WhiteBirdEngine

#endif

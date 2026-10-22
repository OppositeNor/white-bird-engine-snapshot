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
#ifndef WBE_FILE_MATERIAL_TEXTURE_CONTRACT_RESOLVER_HH
#define WBE_FILE_MATERIAL_TEXTURE_CONTRACT_RESOLVER_HH

#include "function/render/material_texture_contract.hh"
#include "function/render/shader_metadata.hh"
#include <optional>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class MaterialTextureContractResolver
 * @brief Resolves and validates the shared material texture contract across shader stages.
 */
class MaterialTextureContractResolver final {
public:
    /**
     * @brief Construct a stateless resolver.
     */
    MaterialTextureContractResolver() = default;
    /**
     * @brief Destroy a resolver.
     */
    ~MaterialTextureContractResolver() = default;
    /**
     * @brief Copy a resolver.
     */
    MaterialTextureContractResolver(const MaterialTextureContractResolver&) = default;
    /**
     * @brief Move a resolver.
     */
    MaterialTextureContractResolver(MaterialTextureContractResolver&&) noexcept = default;
    /**
     * @brief Copy a resolver.
     */
    MaterialTextureContractResolver& operator=(const MaterialTextureContractResolver&) = default;
    /**
     * @brief Move a resolver.
     */
    MaterialTextureContractResolver& operator=(MaterialTextureContractResolver&&) noexcept = default;

    /**
     * @brief Resolve one graphics pipeline's global material contract.
     * @param p_geometry_metadata Optional geometry-stage metadata.
     * @return A contract, or std::nullopt when neither global contract variable is declared.
     * @throws std::runtime_error When variables, annotations, layouts, or stage contracts are malformed or incompatible.
     */
    std::optional<MaterialTextureContract> resolve(const ShaderMetadata& p_vertex_metadata,
        const ShaderMetadata* p_geometry_metadata,
        const ShaderMetadata& p_fragment_metadata,
        std::string_view p_material_textures_variable_name,
        std::string_view p_material_params_variable_name) const;
};

} // namespace WhiteBirdEngine

#endif

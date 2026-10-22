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
#ifndef WBE_FILE_MATERIAL_TEXTURE_CONTRACT_HH
#define WBE_FILE_MATERIAL_TEXTURE_CONTRACT_HH

#include "function/render/shader_role.hh"
#include <cstddef>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class MaterialTextureContract
 * @brief Backend-neutral reflected mapping from material texture roles to index fields.
 */
class MaterialTextureContract final {
public:
    /**
     * @brief One shader-defined material texture index field.
     */
    struct TextureField {
        /** Exact shader-defined texture role. */
        ShaderRole role;
        /** Byte offset inside one material table element. */
        size_t offset = 0;
    };

    /**
     * @brief Construct an empty contract.
     */
    MaterialTextureContract() = default;
    /**
     * @brief Destroy owned role fields.
     */
    ~MaterialTextureContract() = default;
    /**
     * @brief Copy a contract.
     */
    MaterialTextureContract(const MaterialTextureContract&) = default;
    /**
     * @brief Move a contract.
     */
    MaterialTextureContract(MaterialTextureContract&&) noexcept = default;
    /**
     * @brief Copy a contract.
     */
    MaterialTextureContract& operator=(const MaterialTextureContract&) = default;
    /**
     * @brief Move a contract.
     */
    MaterialTextureContract& operator=(MaterialTextureContract&&) noexcept = default;

    /**
     * @brief Test exact role, offset, and table-layout compatibility with another stage contract.
     */
    bool matches(const MaterialTextureContract& p_other) const;

    /** Texture index fields in shader declaration order. */
    std::vector<TextureField> texture_fields;
    /** Byte offset of the material array inside the parameter buffer. */
    size_t table_offset = 0;
    /** Byte stride between material array elements. */
    size_t table_stride = 0;
    /** Total reflected byte size of the material array. */
    size_t table_size = 0;
    /** Number of material array elements. */
    size_t material_count = 0;
};

} // namespace WhiteBirdEngine

#endif

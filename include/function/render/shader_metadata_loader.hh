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
#ifndef WBE_FILE_SHADER_METADATA_LOADER_HH
#define WBE_FILE_SHADER_METADATA_LOADER_HH

#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "resource/resource_unit/resource_unit.hh"

namespace WhiteBirdEngine {

/**
 * @class ShaderMetadataLoader
 * @brief Loads backend-neutral reflection metadata belonging to shader resources.
 */
class ShaderMetadataLoader final {
public:
    /**
     * @brief Construct a stateless loader.
     */
    ShaderMetadataLoader() = default;
    /**
     * @brief Destroy a loader.
     */
    ~ShaderMetadataLoader() = default;
    /**
     * @brief Copy a loader.
     */
    ShaderMetadataLoader(const ShaderMetadataLoader&) = default;
    /**
     * @brief Move a loader.
     */
    ShaderMetadataLoader(ShaderMetadataLoader&&) noexcept = default;
    /**
     * @brief Copy a loader.
     */
    ShaderMetadataLoader& operator=(const ShaderMetadataLoader&) = default;
    /**
     * @brief Move a loader.
     */
    ShaderMetadataLoader& operator=(ShaderMetadataLoader&&) noexcept = default;

    /**
     * @brief Load and deserialize normalized metadata belonging to a shader resource.
     * @throws std::runtime_error When the shader, metadata path, or metadata file is invalid.
     */
    ShaderMetadata load(RefRes<RUShader> p_shader) const;
};

} // namespace WhiteBirdEngine

#endif

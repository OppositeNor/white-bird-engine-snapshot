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
#ifndef WBE_FILE_SHADER_METADATA_QUERY_HH
#define WBE_FILE_SHADER_METADATA_QUERY_HH

#include "function/render/shader_metadata.hh"
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class ShaderMetadataQuery
 * @brief Backend-neutral lookup and uniqueness checks for reflected shader metadata.
 *
 * Returned references and pointers remain valid only while the queried metadata remains alive and unmodified.
 */
class ShaderMetadataQuery final {
public:
    /**
     * @brief Construct a stateless query helper.
     */
    ShaderMetadataQuery() = default;
    /**
     * @brief Destroy a query helper.
     */
    ~ShaderMetadataQuery() = default;
    /**
     * @brief Copy a query helper.
     */
    ShaderMetadataQuery(const ShaderMetadataQuery&) = default;
    /**
     * @brief Move a query helper.
     */
    ShaderMetadataQuery(ShaderMetadataQuery&&) noexcept = default;
    /**
     * @brief Copy a query helper.
     */
    ShaderMetadataQuery& operator=(const ShaderMetadataQuery&) = default;
    /**
     * @brief Move a query helper.
     */
    ShaderMetadataQuery& operator=(ShaderMetadataQuery&&) noexcept = default;

    /**
     * @brief Resolve a stage entry point, falling back to the only reflected entry for the stage.
     * @throws std::runtime_error When the requested entry is missing or an omitted name is ambiguous.
     */
    const ShaderMetadataEntryPoint& resolve_entry_point(
        const ShaderMetadata& p_metadata, std::string_view p_stage, std::string_view p_entry_name) const;

    /**
     * @brief Find one exact stage entry point.
     * @throws std::runtime_error When no matching entry point exists or metadata contains a duplicate.
     */
    const ShaderMetadataEntryPoint& find_entry_point(
        const ShaderMetadata& p_metadata, std::string_view p_stage, std::string_view p_entry_name) const;

    /**
     * @brief Find a unique global shader parameter.
     * @return The parameter, or nullptr when it is absent.
     * @throws std::runtime_error When the parameter name is empty or duplicated.
     */
    const ShaderMetadataParameter* find_parameter(const ShaderMetadata& p_metadata, std::string_view p_name) const;

    /**
     * @brief Find a unique direct field.
     * @return The field, or nullptr when it is absent.
     * @throws std::runtime_error When the field name is empty or duplicated.
     */
    const ShaderMetadataTypeField* find_field(const std::vector<ShaderMetadataTypeField>& p_fields, std::string_view p_name) const;
};

} // namespace WhiteBirdEngine

#endif

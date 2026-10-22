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
#ifndef WBE_FILE_SHADER_ROLE_FIELD_RESOLVER_HH
#define WBE_FILE_SHADER_ROLE_FIELD_RESOLVER_HH

#include "function/render/shader_metadata.hh"
#include "function/render/shader_role.hh"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class ShaderRoleFieldResolver
 * @brief Resolves open resource roles from reflected shader field annotations.
 *
 * This class owns no metadata. Field pointers in results remain valid only while the queried metadata remains alive and
 * unmodified.
 */
class ShaderRoleFieldResolver final {
public:
    /**
     * @brief Select whether nested reflected fields are visited.
     */
    enum class Traversal : uint8_t {
        DIRECT,
        RECURSIVE,
    };

    /**
     * @brief One annotated shader field and its exact resolved role.
     */
    struct Field {
        /** Exact shader-defined role. */
        ShaderRole role;
        /** Non-owning pointer to the annotated reflected field. */
        const ShaderMetadataTypeField* metadata = nullptr;
    };

    /**
     * @brief Construct a stateless resolver.
     */
    ShaderRoleFieldResolver() = default;
    /**
     * @brief Destroy a resolver.
     */
    ~ShaderRoleFieldResolver() = default;
    /**
     * @brief Copy a resolver.
     */
    ShaderRoleFieldResolver(const ShaderRoleFieldResolver&) = default;
    /**
     * @brief Move a resolver.
     */
    ShaderRoleFieldResolver(ShaderRoleFieldResolver&&) noexcept = default;
    /**
     * @brief Copy a resolver.
     */
    ShaderRoleFieldResolver& operator=(const ShaderRoleFieldResolver&) = default;
    /**
     * @brief Move a resolver.
     */
    ShaderRoleFieldResolver& operator=(ShaderRoleFieldResolver&&) noexcept = default;

    /**
     * @brief Resolve one field annotation using its optional string argument or the field name as the default role.
     * @return The resolved field, or std::nullopt when the annotation is absent.
     * @throws std::runtime_error When the annotation or resolved role is malformed or duplicated on the field.
     */
    std::optional<Field> resolve_field(const ShaderMetadataTypeField& p_field, std::string_view p_annotation_name) const;

    /**
     * @brief Collect uniquely named and uniquely hashed annotated roles in declaration order.
     * @throws std::runtime_error When annotations are malformed, roles repeat, or distinct role names collide by hash.
     */
    std::vector<Field> collect_fields(
        const std::vector<ShaderMetadataTypeField>& p_fields, std::string_view p_annotation_name, Traversal p_traversal) const;
};

} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_SHADER_ROLE_HH
#define WBE_FILE_SHADER_ROLE_HH

#include "utils/utils.hh"
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class ShaderRole
 * @brief Owning identity of an open shader-defined resource role.
 *
 * The retained name is authoritative. The hash is an acceleration value and is never sufficient for identity by itself.
 */
class ShaderRole final {
public:
    /**
     * @brief Construct an empty role for later assignment.
     */
    ShaderRole() = default;
    /**
     * @brief Destroy the retained role name.
     */
    ~ShaderRole() = default;
    /**
     * @brief Copy a role identity.
     */
    ShaderRole(const ShaderRole&) = default;
    /**
     * @brief Move a role identity.
     */
    ShaderRole(ShaderRole&&) noexcept = default;
    /**
     * @brief Copy a role identity.
     */
    ShaderRole& operator=(const ShaderRole&) = default;
    /**
     * @brief Move a role identity.
     */
    ShaderRole& operator=(ShaderRole&&) noexcept = default;

    /**
     * @brief Construct an exact role identity from its name.
     * @throws std::runtime_error When the role name is empty or contains NUL.
     */
    explicit ShaderRole(std::string_view p_name);

    /**
     * @brief Return the authoritative role name.
     */
    const std::string& get_name() const;
    /**
     * @brief Return the acceleration hash derived from the role name.
     */
    HashCode get_hash() const;
    /**
     * @brief Match another role only when both its name and hash agree.
     */
    bool matches(std::string_view p_name, HashCode p_hash) const;

private:
    std::string name;
    HashCode hash = 0;
};

} // namespace WhiteBirdEngine

#endif

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
#include "function/render/shader_role.hh"
#include "utils/utils.hh"
#include <stdexcept>
#include <string_view>

namespace WhiteBirdEngine {

ShaderRole::ShaderRole(std::string_view p_name) : name(p_name), hash(dynam_hash(p_name)) {
    if (p_name.empty() || p_name.contains('\0')) {
        throw std::runtime_error("Failed to create shader role: role name must be non-empty and must not contain NUL.");
    }
}

const std::string& ShaderRole::get_name() const {
    return name;
}

HashCode ShaderRole::get_hash() const {
    return hash;
}

bool ShaderRole::matches(std::string_view p_name, HashCode p_hash) const {
    return hash == p_hash && name == p_name;
}

} // namespace WhiteBirdEngine

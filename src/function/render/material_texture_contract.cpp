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
#include "function/render/material_texture_contract.hh"
#include <cstddef>

namespace WhiteBirdEngine {

bool MaterialTextureContract::matches(const MaterialTextureContract& p_other) const {
    if (table_offset != p_other.table_offset || table_stride != p_other.table_stride || table_size != p_other.table_size ||
        material_count != p_other.material_count || texture_fields.size() != p_other.texture_fields.size()) {
        return false;
    }
    for (size_t i = 0; i < texture_fields.size(); ++i) {
        const TextureField& left = texture_fields[i];
        const TextureField& right = p_other.texture_fields[i];
        if (!left.role.matches(right.role.get_name(), right.role.get_hash()) || left.offset != right.offset) {
            return false;
        }
    }
    return true;
}

} // namespace WhiteBirdEngine

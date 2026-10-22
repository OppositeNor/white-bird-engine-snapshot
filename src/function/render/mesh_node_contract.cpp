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
#include "function/render/mesh_node_contract.hh"

namespace WhiteBirdEngine {

bool MeshNodeContract::matches(const MeshNodeContract& p_other) const {
    return element_stride == p_other.element_stride && transform_offset == p_other.transform_offset &&
           normal_transform_offset == p_other.normal_transform_offset;
}

} // namespace WhiteBirdEngine
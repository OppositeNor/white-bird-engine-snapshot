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
#include "resource/resource_manager/smr_resource_manager.hh"
#include "core/allocator/i_allocator.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/defs.hh"
#include <optional>
#include <string_view>

namespace WhiteBirdEngine {

std::optional<std::string_view> SMRResourceManager::try_resolve(std::string_view p_module) const noexcept {
    const ResourceManager* resource_manager = ResourceManager::get_singleton();
    if (resource_manager == nullptr) {
        return std::nullopt;
    }
    const RefRes<RUScript> script = resource_manager->try_get_global_resource(p_module).force_cast<RUScript>();
    if (script == MEM_NULL || script->source == MEM_NULL) {
        return std::nullopt;
    }
    WBE_DEBUG_ASSERT(script->size > 0);
    return std::string_view(script->source.get(), script->size - 1);
}

} // namespace WhiteBirdEngine

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
#ifndef WBE_FILE_RT_PARAMS_FULL_MEMORY_BARRIER_VK_HH
#define WBE_FILE_RT_PARAMS_FULL_MEMORY_BARRIER_VK_HH

#include "core/reflection/reflection_defs.hh"
#include <string>

namespace WhiteBirdEngine {

/**
 * @class RTParamsFullMemoryBarrierVK
 * @brief Recipe parameters of RTFullMemoryBarrierVK. Copyable and movable.
 *
 * Deserialization is strict: an unknown key, such as a misspelt parameter, throws instead of being ignored.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParamsFullMemoryBarrierVK {
    /**
     * @brief Graph resource the barrier reports writing, which carries the edge to the task ordered after it.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_RECIPE_RESOURCE(graph))
    std::string resource;
};

} // namespace WhiteBirdEngine

#endif

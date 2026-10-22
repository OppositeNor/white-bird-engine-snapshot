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
#ifndef WBE_FILE_FUNTION_UTILS_HH
#define WBE_FILE_FUNTION_UTILS_HH

#include "core/reflection/reflection_defs.hh"
#include <entt/entt.hpp>
namespace WhiteBirdEngine {

using EntityID = entt::entity;

/**
 * @brief The device domain.
 *
 * Reflected; the lowercase aliases are the names used in render graph recipes.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) DeviceDomain {
    // The host domain
    HOST WBE_META(WBE_SERIALIZATION_ALIASES(host)),
    // The device domain
    DEVICE WBE_META(WBE_SERIALIZATION_ALIASES(device)),
    // Both the host and device domain
    HOST_DEVICE WBE_META(WBE_SERIALIZATION_ALIASES(host_device)),
};

} // namespace WhiteBirdEngine

#endif

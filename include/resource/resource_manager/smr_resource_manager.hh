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
#ifndef WBE_FILE_SMR_RESOURCE_MANAGER_HH
#define WBE_FILE_SMR_RESOURCE_MANAGER_HH

#include "core/script/i_script_module_resolver.hh"
#include <optional>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class SMRResourceManager
 * @brief Resolves imported script modules to the `script` resources of the ResourceManager singleton.
 *
 * A module name is the resource key of its script, so `require("render_graph_utils.render_graph_utils")` loads the script
 * resource `render_graph_utils.render_graph_utils`. The resolved source is chunk-owned and stays valid while the chunk is
 * registered. The name must not belong to a resource of another type. Stateless; neither copyable nor movable, like every
 * IScriptModuleResolver.
 */
class SMRResourceManager final : public IScriptModuleResolver {
public:
    SMRResourceManager() = default;
    virtual ~SMRResourceManager() override = default;
    SMRResourceManager(const SMRResourceManager&) = delete;
    SMRResourceManager(SMRResourceManager&&) noexcept = delete;
    SMRResourceManager& operator=(const SMRResourceManager&) = delete;
    SMRResourceManager& operator=(SMRResourceManager&&) noexcept = delete;

    /**
     * @brief Find the source of the script resource keyed p_module.
     * @return The source without its NUL terminator, or std::nullopt when the resource manager is unavailable or no
     * registered chunk holds the key.
     */
    virtual std::optional<std::string_view> try_resolve(std::string_view p_module) const noexcept override;
};

} // namespace WhiteBirdEngine

#endif

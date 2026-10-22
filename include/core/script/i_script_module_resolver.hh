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
#ifndef WBE_FILE_I_SCRIPT_MODULE_RESOLVER_HH
#define WBE_FILE_I_SCRIPT_MODULE_RESOLVER_HH

#include <optional>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class IScriptModuleResolver
 * @brief Finds the source of a module a script imports, such as through Lua's `require`.
 *
 * Resolvers are called by script backends while a script is running, from the thread that uses the backend. They are
 * neither copyable nor movable.
 */
class IScriptModuleResolver {
public:
    IScriptModuleResolver() = default;
    virtual ~IScriptModuleResolver() = default;
    IScriptModuleResolver(const IScriptModuleResolver&) = delete;
    IScriptModuleResolver(IScriptModuleResolver&&) noexcept = delete;
    IScriptModuleResolver& operator=(const IScriptModuleResolver&) = delete;
    IScriptModuleResolver& operator=(IScriptModuleResolver&&) noexcept = delete;

    /**
     * @brief Find the source of a module.
     *
     * Noexcept, because it runs inside the script runtime, which exceptions must not cross; a failure that is not "no such
     * module" terminates the program.
     *
     * @param p_module The module name the script imports.
     * @return The module source, valid at least until the backend has compiled it, or std::nullopt when no module has the
     * name.
     */
    virtual std::optional<std::string_view> try_resolve(std::string_view p_module) const noexcept = 0;
};

} // namespace WhiteBirdEngine

#endif

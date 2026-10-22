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
#ifndef WBE_FILE_SB_LUA_HH
#define WBE_FILE_SB_LUA_HH

#include "core/data/data_value.hh"
#include "core/script/i_script_backend.hh"
#include "core/script/i_script_module_resolver.hh"
#include "core/script/script_sandbox_config.hh"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

struct lua_State;
struct lua_Debug;

namespace WhiteBirdEngine {

/**
 * @class SBLua
 * @brief Lua implementation of IScriptBackend, backed by one lua_State.
 *
 * Scripts get the base, string, table, math, and utf8 libraries. File, operating-system, debug, and package access are
 * not loaded; `dofile` and `loadfile` are removed, and `load` only accepts text chunks. A module is a Lua chunk that returns
 * a table of functions; each module runs with its own global table whose missing names fall back to the shared standard
 * libraries. The hash seed is fixed, so table iteration order is reproducible between runs.
 *
 * `require(name)` returns the export table of the module loaded under the name, loading it first from the module
 * resolver when it is not loaded yet. Every importer shares that table. Requiring a module that is still loading, a name
 * the resolver does not know, or any unloaded module without a resolver raises a Lua error.
 *
 * Conversions to DataValue map nil, booleans, integers, floats, and strings directly. A table whose keys are exactly
 * 1..n becomes a list and a table whose keys are all strings becomes a map; an empty table becomes an empty list.
 * Metatables are ignored. Functions, userdata, threads, non-finite numbers, mixed or non-string keys, cyclic tables, and
 * nesting deeper than MAX_VALUE_DEPTH fail with the value's path.
 *
 * Errors raised while no script is running, such as running out of memory while converting arguments, are not
 * recoverable and abort through Lua's panic handler.
 */
class SBLua final : public IScriptBackend {
public:
    /**
     * @brief Maximum nesting depth of converted values.
     */
    static constexpr size_t MAX_VALUE_DEPTH = 128;

    /**
     * @brief Create a backend without resource limits.
     */
    SBLua();
    /**
     * @brief Create a backend with the given resource limits.
     * @throws std::runtime_error If the Lua state cannot be created within the memory limit.
     */
    explicit SBLua(const ScriptSandboxConfig& p_config);
    virtual ~SBLua() override;
    SBLua(const SBLua&) = delete;
    SBLua(SBLua&&) noexcept = delete;
    SBLua& operator=(const SBLua&) = delete;
    SBLua& operator=(SBLua&&) noexcept = delete;

    virtual void load_module(std::string_view p_module, std::string_view p_source) override;
    virtual void set_module_resolver(const IScriptModuleResolver* p_resolver) override;
    virtual DataValue call(std::string_view p_module, std::string_view p_function, std::span<const DataValue> p_args) override;
    virtual bool has_function(std::string_view p_module, std::string_view p_function) const override;

private:
    static void* allocate(void* p_user_data, void* p_pointer, size_t p_old_size, size_t p_new_size);
    static void count_instructions(lua_State* p_state, lua_Debug* p_debug);
    static int require_module(lua_State* p_state);
    void reset_instruction_budget();
    void run_sandbox_prelude();
    void push_module_exports(std::string_view p_module) const;

    ScriptSandboxConfig config;
    lua_State* state = nullptr;
    const IScriptModuleResolver* module_resolver = nullptr;
    size_t allocated_bytes = 0;
    uint64_t executed_instructions = 0;
};

} // namespace WhiteBirdEngine

#endif

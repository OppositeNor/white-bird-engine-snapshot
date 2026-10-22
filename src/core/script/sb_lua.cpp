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
#include "core/script/sb_lua.hh"
#include "core/data/data_value.hh"
#include "core/script/i_script_module_resolver.hh"
#include "core/script/script_sandbox_config.hh"
#include "utils/defs.hh"
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

namespace {

// Instructions executed between two instruction-limit checks.
constexpr int INSTRUCTION_HOOK_INTERVAL = 1000;

// Fixed hash seed, so table iteration order is reproducible between runs.
constexpr unsigned LUA_HASH_SEED = 0x57424531U;

// Its address identifies the registry entry holding the table that maps module names to their export tables.
constexpr char MODULES_REGISTRY_KEY = 0;

// Pure-Lua sandbox setup run once per state: removes file loading and restricts load() to text chunks.
constexpr std::string_view SANDBOX_PRELUDE = R"(
local raw_load = load
load = function(chunk, chunk_name, _, env)
    return raw_load(chunk, chunk_name, "t", env)
end
dofile = nil
loadfile = nil
)";

// Restores the Lua stack top when the scope ends, including when an exception leaves it.
class LuaStackGuard final {
public:
    LuaStackGuard() = delete;
    explicit LuaStackGuard(lua_State* p_state) : state(p_state), top(lua_gettop(p_state)) {
    }
    ~LuaStackGuard() {
        lua_settop(state, top);
    }
    LuaStackGuard(const LuaStackGuard&) = delete;
    LuaStackGuard(LuaStackGuard&&) noexcept = delete;
    LuaStackGuard& operator=(const LuaStackGuard&) = delete;
    LuaStackGuard& operator=(LuaStackGuard&&) noexcept = delete;

private:
    lua_State* state;
    int top;
};

// Opens the sandbox libraries, creates the module table, and installs the `require` function passed as the only
// argument; run under lua_pcall so allocation failures are reported.
int initialize_state(lua_State* p_state) {
    luaL_openselectedlibs(p_state, LUA_GLIBK | LUA_STRLIBK | LUA_TABLIBK | LUA_MATHLIBK | LUA_UTF8LIBK, 0);
    lua_newtable(p_state);
    lua_rawsetp(p_state, LUA_REGISTRYINDEX, &MODULES_REGISTRY_KEY);
    lua_pushvalue(p_state, 1);
    lua_setglobal(p_state, "require");
    return 0;
}

// Give the chunk at the stack top its own globals; missing names fall back to the shared standard libraries. Returns false
// when the chunk has no environment upvalue.
bool set_module_environment(lua_State* p_state) {
    lua_newtable(p_state);
    lua_createtable(p_state, 0, 1);
    lua_pushglobaltable(p_state);
    lua_setfield(p_state, -2, "__index");
    lua_setmetatable(p_state, -2);
    return lua_setupvalue(p_state, -2, 1) != nullptr;
}

// Message handler of protected calls: appends a traceback to the error message.
int traceback_message_handler(lua_State* p_state) {
    const char* message = lua_tostring(p_state, 1);
    luaL_traceback(p_state, p_state, message != nullptr ? message : "(error object is not a string)", 1);
    return 1;
}

// Prints unprotected errors before Lua aborts the process.
int report_panic(lua_State* p_state) {
    const char* message = lua_tostring(p_state, -1);
    std::println(stderr, "Unrecoverable Lua error: %s\n", message != nullptr ? message : "(error object is not a string)");
    return 0;
}

std::string get_error_message(lua_State* p_state) {
    const char* message = lua_tostring(p_state, -1);
    return message != nullptr ? std::string(message) : std::string("(error object is not a string)");
}

void ensure_stack(lua_State* p_state, int p_slots, std::string_view p_path) {
    if (lua_checkstack(p_state, p_slots) == 0) {
        throw std::runtime_error(std::format("Failed to convert script value at {}: the Lua stack is exhausted.", p_path));
    }
}

void push_script_value(lua_State* p_state, const DataValue& p_value, size_t p_depth) {
    if (p_depth > SBLua::MAX_VALUE_DEPTH) {
        throw std::runtime_error(std::format("Failed to pass script value: nesting exceeds {} levels.", SBLua::MAX_VALUE_DEPTH));
    }
    ensure_stack(p_state, 3, "argument");
    switch (p_value.get_kind()) {
    case DataValueKind::NIL:
        lua_pushnil(p_state);
        return;
    case DataValueKind::BOOLEAN:
        lua_pushboolean(p_state, p_value.as_boolean() ? 1 : 0);
        return;
    case DataValueKind::INTEGER:
        lua_pushinteger(p_state, static_cast<lua_Integer>(p_value.as_integer()));
        return;
    case DataValueKind::FLOAT:
        lua_pushnumber(p_state, static_cast<lua_Number>(p_value.as_float()));
        return;
    case DataValueKind::STRING: {
        const std::string& string_value = p_value.as_string();
        lua_pushlstring(p_state, string_value.data(), string_value.size());
        return;
    }
    case DataValueKind::LIST: {
        const DataValue::List& list = p_value.as_list();
        lua_createtable(p_state, static_cast<int>(std::min<size_t>(list.size(), INT32_MAX)), 0);
        for (size_t i = 0; i < list.size(); ++i) {
            push_script_value(p_state, list[i], p_depth + 1);
            lua_rawseti(p_state, -2, static_cast<lua_Integer>(i + 1));
        }
        return;
    }
    case DataValueKind::MAP: {
        const DataValue::Map& map = p_value.as_map();
        lua_createtable(p_state, 0, static_cast<int>(std::min<size_t>(map.size(), INT32_MAX)));
        for (const DataValue::MapEntry& entry : map) {
            lua_pushlstring(p_state, entry.first.data(), entry.first.size());
            push_script_value(p_state, entry.second, p_depth + 1);
            lua_rawset(p_state, -3);
        }
        return;
    }
    }
    throw std::runtime_error("Failed to pass script value: unknown kind.");
}

DataValue to_script_value(
    lua_State* p_state, int p_index, const std::string& p_path, std::vector<const void*>& p_active_tables, size_t p_depth);

DataValue table_to_script_value( // NOLINT
    lua_State* p_state,
    int p_index,
    const std::string& p_path,
    std::vector<const void*>& p_active_tables,
    size_t p_depth) {
    const void* table_identity = lua_topointer(p_state, p_index);
    if (std::ranges::find(p_active_tables, table_identity) != p_active_tables.end()) {
        throw std::runtime_error(std::format("Failed to convert script value at {}: the table contains itself.", p_path));
    }
    if (p_depth > SBLua::MAX_VALUE_DEPTH) {
        throw std::runtime_error(
            std::format("Failed to convert script value at {}: nesting exceeds {} levels.", p_path, SBLua::MAX_VALUE_DEPTH));
    }
    ensure_stack(p_state, 4, p_path);

    size_t entry_count = 0;
    bool has_only_string_keys = true;
    bool has_only_positive_integer_keys = true;
    lua_Integer largest_integer_key = 0;
    lua_pushnil(p_state);
    while (lua_next(p_state, p_index) != 0) {
        ++entry_count;
        if (lua_type(p_state, -2) == LUA_TSTRING) {
            has_only_positive_integer_keys = false;
        } else if (lua_isinteger(p_state, -2) != 0 && lua_tointeger(p_state, -2) >= 1) {
            has_only_string_keys = false;
            largest_integer_key = std::max(largest_integer_key, lua_tointeger(p_state, -2));
        } else {
            has_only_string_keys = false;
            has_only_positive_integer_keys = false;
        }
        lua_pop(p_state, 1);
    }
    if (entry_count == 0) {
        return DataValue::make_list();
    }

    p_active_tables.push_back(table_identity);
    DataValue result;
    if (has_only_positive_integer_keys && std::cmp_equal(largest_integer_key, entry_count)) {
        result = DataValue::make_list();
        for (size_t i = 1; i <= entry_count; ++i) {
            lua_rawgeti(p_state, p_index, static_cast<lua_Integer>(i));
            result.push_back(
                to_script_value(p_state, lua_gettop(p_state), std::format("{}[{}]", p_path, i), p_active_tables, p_depth + 1));
            lua_pop(p_state, 1);
        }
    } else if (has_only_string_keys) {
        result = DataValue::make_map();
        lua_pushnil(p_state);
        while (lua_next(p_state, p_index) != 0) {
            size_t key_length = 0;
            const char* key_data = lua_tolstring(p_state, -2, &key_length);
            const std::string_view key(key_data, key_length);
            result.set_entry(key,
                to_script_value(p_state, lua_gettop(p_state), std::format("{}.{}", p_path, key), p_active_tables, p_depth + 1));
            lua_pop(p_state, 1);
        }
    } else {
        throw std::runtime_error(std::format(
            "Failed to convert script value at {}: a table must have only the keys 1..n (a list) or only string keys (a map).", p_path));
    }
    p_active_tables.pop_back();
    return result;
}

DataValue to_script_value(
    lua_State* p_state, int p_index, const std::string& p_path, std::vector<const void*>& p_active_tables, size_t p_depth) {
    switch (lua_type(p_state, p_index)) {
    case LUA_TNIL:
        return {};
    case LUA_TBOOLEAN:
        return DataValue::make_boolean(lua_toboolean(p_state, p_index) != 0);
    case LUA_TNUMBER: {
        if (lua_isinteger(p_state, p_index) != 0) {
            return DataValue::make_integer(static_cast<int64_t>(lua_tointeger(p_state, p_index)));
        }
        const double number = static_cast<double>(lua_tonumber(p_state, p_index));
        if (!std::isfinite(number)) {
            throw std::runtime_error(std::format("Failed to convert script value at {}: the number is not finite.", p_path));
        }
        return DataValue::make_float(number);
    }
    case LUA_TSTRING: {
        size_t length = 0;
        const char* data = lua_tolstring(p_state, p_index, &length);
        return DataValue::make_string(std::string_view(data, length));
    }
    case LUA_TTABLE:
        return table_to_script_value(p_state, p_index, p_path, p_active_tables, p_depth);
    default:
        throw std::runtime_error(std::format("Failed to convert script value at {}: {} values cannot cross the script boundary.",
            p_path,
            lua_typename(p_state, lua_type(p_state, p_index))));
    }
}

} // namespace

SBLua::SBLua() : SBLua(ScriptSandboxConfig{}) {
}

SBLua::SBLua(const ScriptSandboxConfig& p_config) : config(p_config) {
    state = lua_newstate(&SBLua::allocate, this, LUA_HASH_SEED);
    if (state == nullptr) {
        throw std::runtime_error("Failed to create Lua state: out of memory.");
    }
    lua_atpanic(state, &report_panic);
    lua_pushcfunction(state, &initialize_state);
    lua_pushcfunction(state, &SBLua::require_module);
    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
        const std::string message = get_error_message(state);
        lua_close(state);
        state = nullptr;
        throw std::runtime_error(std::format("Failed to initialize Lua state: {}", message));
    }
    run_sandbox_prelude();
    if (config.instruction_limit != 0) {
        lua_sethook(state, &SBLua::count_instructions, LUA_MASKCOUNT, INSTRUCTION_HOOK_INTERVAL);
    }
}

SBLua::~SBLua() {
    if (state != nullptr) {
        lua_close(state);
    }
}

void SBLua::load_module(std::string_view p_module, std::string_view p_source) {
    if (p_module.empty()) {
        throw std::runtime_error("Failed to load script module: the module name is empty.");
    }
    LuaStackGuard guard(state);
    reset_instruction_budget();
    lua_pushcfunction(state, &traceback_message_handler);
    const int message_handler = lua_gettop(state);
    const std::string chunk_name = std::format("={}", p_module);
    if (luaL_loadbufferx(state, p_source.data(), p_source.size(), chunk_name.c_str(), "t") != LUA_OK) {
        throw std::runtime_error(std::format("Failed to compile script module '{}': {}", p_module, get_error_message(state)));
    }
    if (!set_module_environment(state)) {
        throw std::runtime_error(std::format("Failed to load script module '{}': the chunk has no environment.", p_module));
    }
    if (lua_pcall(state, 0, 1, message_handler) != LUA_OK) {
        throw std::runtime_error(std::format("Failed to run script module '{}': {}", p_module, get_error_message(state)));
    }
    if (lua_type(state, -1) != LUA_TTABLE) {
        throw std::runtime_error(std::format("Failed to load script module '{}': the module must return a table of exports, but returned {}.",
            p_module,
            luaL_typename(state, -1)));
    }
    lua_rawgetp(state, LUA_REGISTRYINDEX, &MODULES_REGISTRY_KEY);
    lua_pushlstring(state, p_module.data(), p_module.size());
    lua_pushvalue(state, -3);
    lua_rawset(state, -3);
}

void SBLua::set_module_resolver(const IScriptModuleResolver* p_resolver) {
    module_resolver = p_resolver;
}

DataValue SBLua::call(std::string_view p_module, std::string_view p_function, std::span<const DataValue> p_args) {
    LuaStackGuard guard(state);
    reset_instruction_budget();
    lua_pushcfunction(state, &traceback_message_handler);
    const int message_handler = lua_gettop(state);
    push_module_exports(p_module);
    if (lua_type(state, -1) != LUA_TTABLE) {
        throw std::runtime_error(std::format("Failed to call script function '{}.{}': the module is not loaded.", p_module, p_function));
    }
    lua_pushlstring(state, p_function.data(), p_function.size());
    lua_rawget(state, -2);
    if (lua_type(state, -1) != LUA_TFUNCTION) {
        throw std::runtime_error(std::format(
            "Failed to call script function '{}.{}': the module does not export a function with this name.", p_module, p_function));
    }
    if (p_args.size() > static_cast<size_t>(INT32_MAX)) {
        throw std::runtime_error(std::format("Failed to call script function '{}.{}': too many arguments.", p_module, p_function));
    }
    for (const DataValue& arg : p_args) {
        push_script_value(state, arg, 0);
    }
    if (lua_pcall(state, static_cast<int>(p_args.size()), 1, message_handler) != LUA_OK) {
        throw std::runtime_error(std::format("Script function '{}.{}' failed: {}", p_module, p_function, get_error_message(state)));
    }
    std::vector<const void*> active_tables;
    return to_script_value(state, lua_gettop(state), "result", active_tables, 0);
}

bool SBLua::has_function(std::string_view p_module, std::string_view p_function) const {
    LuaStackGuard guard(state);
    push_module_exports(p_module);
    if (lua_type(state, -1) != LUA_TTABLE) {
        return false;
    }
    lua_pushlstring(state, p_function.data(), p_function.size());
    lua_rawget(state, -2);
    return lua_type(state, -1) == LUA_TFUNCTION;
}

void* SBLua::allocate(void* p_user_data, void* p_pointer, size_t p_old_size, size_t p_new_size) {
    WBE_DEBUG_ASSERT(p_user_data != nullptr);
    auto* backend = static_cast<SBLua*>(p_user_data);
    // Without a block, Lua passes the kind of object being created in p_old_size instead of a size.
    const size_t old_size = p_pointer == nullptr ? 0 : p_old_size;
    if (p_new_size == 0) {
        std::free(p_pointer); // NOLINT(cppcoreguidelines-no-malloc)
        backend->allocated_bytes -= old_size;
        return nullptr;
    }
    const size_t memory_limit = backend->config.memory_limit;
    if (memory_limit != 0 && p_new_size > old_size && backend->allocated_bytes - old_size + p_new_size > memory_limit) {
        return nullptr;
    }
    void* result = std::realloc(p_pointer, p_new_size); // NOLINT(cppcoreguidelines-no-malloc)
    if (result != nullptr) {
        backend->allocated_bytes = backend->allocated_bytes - old_size + p_new_size;
    }
    return result;
}

void SBLua::count_instructions(lua_State* p_state, lua_Debug* /*p_debug*/) {
    void* user_data = nullptr;
    lua_getallocf(p_state, &user_data);
    WBE_DEBUG_ASSERT(user_data != nullptr);
    auto* backend = static_cast<SBLua*>(user_data);
    backend->executed_instructions += INSTRUCTION_HOOK_INTERVAL;
    if (backend->executed_instructions > backend->config.instruction_limit) {
        // Leaves this frame through Lua's error mechanism; the frame holds no objects with destructors.
        luaL_error(p_state, "instruction limit of %llu exceeded", backend->config.instruction_limit);
    }
}

int SBLua::require_module(lua_State* p_state) {
    // Errors leave this frame through Lua's error mechanism; the frame holds no objects with destructors.
    constexpr int NAME_INDEX = 1;
    constexpr int MODULES_INDEX = 2;
    size_t name_length = 0;
    const char* name = luaL_checklstring(p_state, NAME_INDEX, &name_length);
    lua_settop(p_state, NAME_INDEX);
    lua_rawgetp(p_state, LUA_REGISTRYINDEX, &MODULES_REGISTRY_KEY);
    lua_pushvalue(p_state, NAME_INDEX);
    lua_rawget(p_state, MODULES_INDEX);
    if (lua_type(p_state, -1) == LUA_TTABLE) {
        return 1;
    }
    // A module that is still loading is marked with false.
    if (lua_type(p_state, -1) == LUA_TBOOLEAN) {
        return luaL_error(p_state, "module '%s' is required while it is still loading (cyclic require)", name);
    }
    lua_pop(p_state, 1);
    void* user_data = nullptr;
    lua_getallocf(p_state, &user_data);
    WBE_DEBUG_ASSERT(user_data != nullptr);
    const auto* backend = static_cast<const SBLua*>(user_data);
    if (backend->module_resolver == nullptr) {
        return luaL_error(p_state, "module '%s' is not loaded and no module resolver is set", name);
    }
    const std::optional<std::string_view> source = backend->module_resolver->try_resolve(std::string_view(name, name_length));
    if (!source.has_value()) {
        return luaL_error(p_state, "module '%s' not found", name);
    }
    const char* chunk_name = lua_pushfstring(p_state, "=%s", name);
    if (luaL_loadbufferx(p_state, source->data(), source->size(), chunk_name, "t") != LUA_OK) {
        return lua_error(p_state);
    }
    if (!set_module_environment(p_state)) {
        return luaL_error(p_state, "module '%s' has no environment", name);
    }
    lua_pushvalue(p_state, NAME_INDEX);
    lua_pushboolean(p_state, 0);
    lua_rawset(p_state, MODULES_INDEX);
    const bool loaded = lua_pcall(p_state, 0, 1, 0) == LUA_OK;
    const bool returned_table = loaded && lua_type(p_state, -1) == LUA_TTABLE;
    // Store the exports, or clear the loading mark so that a later require reports the failure again.
    lua_pushvalue(p_state, NAME_INDEX);
    if (returned_table) {
        lua_pushvalue(p_state, -2);
    } else {
        lua_pushnil(p_state);
    }
    lua_rawset(p_state, MODULES_INDEX);
    if (!loaded) {
        return lua_error(p_state);
    }
    if (!returned_table) {
        return luaL_error(p_state, "module '%s' must return a table of exports, but returned %s", name, luaL_typename(p_state, -1));
    }
    return 1;
}

void SBLua::reset_instruction_budget() {
    executed_instructions = 0;
}

void SBLua::run_sandbox_prelude() {
    // A successful protected call leaves the stack empty; on failure the state is closed, so no stack guard is used.
    if (luaL_loadbufferx(state, SANDBOX_PRELUDE.data(), SANDBOX_PRELUDE.size(), "=sandbox_prelude", "t") != LUA_OK ||
        lua_pcall(state, 0, 0, 0) != LUA_OK) {
        const std::string message = get_error_message(state);
        lua_close(state);
        state = nullptr;
        throw std::runtime_error(std::format("Failed to initialize Lua sandbox: {}", message));
    }
}

void SBLua::push_module_exports(std::string_view p_module) const {
    lua_rawgetp(state, LUA_REGISTRYINDEX, &MODULES_REGISTRY_KEY);
    lua_pushlstring(state, p_module.data(), p_module.size());
    lua_rawget(state, -2);
    lua_remove(state, -2);
}

} // namespace WhiteBirdEngine

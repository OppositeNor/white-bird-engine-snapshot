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
#ifndef WBE_FILE_SB_LUA_TEST_HH
#define WBE_FILE_SB_LUA_TEST_HH

#include "../reflection_test/reflection_test_data.hh"
#include "core/data/data_value.hh"
#include "core/script/i_script_module_resolver.hh"
#include "core/script/sb_lua.hh"
#include "core/script/script_sandbox_config.hh"
#include "glm/ext/vector_float3.hpp"
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WBE = WhiteBirdEngine;

constexpr std::string_view SB_LUA_TEST_MODULE = R"(
local exports = {}

function exports.add(left, right)
    return left + right
end

function exports.echo(value)
    return value
end

function exports.nothing()
end

function exports.kinds()
    return { integer = 1, float = 1.5, whole_float = 2.0, text = "a\0b", flag = false, list = { 10, 20 }, empty = {} }
end

function exports.describe(settings)
    local tasks = {}
    for i = 1, settings.light_count do
        tasks[#tasks + 1] = { id = "shadow_" .. i, type = "run_graphics_pipeline" }
    end
    return { tasks = tasks, size = settings.base_size / 2 }
end

function exports.fail()
    error("boom")
end

function exports.return_function()
    return { callback = function() end }
end

function exports.return_mixed()
    return { 1, 2, key = "value" }
end

function exports.return_sparse()
    return { [1] = "a", [3] = "c" }
end

function exports.return_cycle()
    local cycle = {}
    cycle.self = cycle
    return cycle
end

function exports.return_shared()
    local shared = { 1 }
    return { first = shared, second = shared }
end

function exports.return_nan()
    return 0 / 0
end

function exports.make_struct(value)
    return { si32_test = value, str_test = "from lua", vec3_test = { x = 1, y = 2, z = 3 } }
end

function exports.double_struct(source)
    source.si32_test = source.si32_test * 2
    source.str_test = source.str_test .. "!"
    return source
end

return exports
)";

inline int64_t lua_test_add(WBE::SBLua& p_backend, int64_t p_left, int64_t p_right) {
    const std::array<WBE::DataValue, 2> args = {WBE::DataValue::make_integer(p_left), WBE::DataValue::make_integer(p_right)};
    return p_backend.call("test", "add", args).as_integer();
}

TEST(WBESBLuaTest, CallsExportedFunctions) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);

    EXPECT_EQ(lua_test_add(backend, 2, 3), 5);
    EXPECT_TRUE(backend.call("test", "nothing", {}).is_nil());
    EXPECT_TRUE(backend.has_function("test", "add"));
    EXPECT_FALSE(backend.has_function("test", "missing"));
    EXPECT_FALSE(backend.has_function("other", "add"));
}

TEST(WBESBLuaTest, ConvertsResultKinds) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);
    const WBE::DataValue result = backend.call("test", "kinds", {});

    ASSERT_EQ(result.get_kind(), WBE::DataValueKind::MAP);
    EXPECT_EQ(result.find("integer")->get_kind(), WBE::DataValueKind::INTEGER);
    EXPECT_EQ(result.find("float")->get_kind(), WBE::DataValueKind::FLOAT);
    EXPECT_EQ(result.find("whole_float")->get_kind(), WBE::DataValueKind::FLOAT);
    EXPECT_EQ(result.find("text")->as_string(), std::string("a\0b", 3));
    EXPECT_FALSE(result.find("flag")->as_boolean());
    ASSERT_EQ(result.find("list")->as_list().size(), 2U);
    EXPECT_EQ(result.find("list")->as_list()[1].as_integer(), 20);
    EXPECT_EQ(result.find("empty")->get_kind(), WBE::DataValueKind::LIST);
    EXPECT_TRUE(result.find("empty")->as_list().empty());
}

TEST(WBESBLuaTest, ArgumentsRoundTrip) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);
    WBE::DataValue argument;
    argument.set_entry("name", WBE::DataValue::make_string("value"));
    argument.set_entry("number", WBE::DataValue::make_float(0.5));
    WBE::DataValue nested;
    nested.push_back(WBE::DataValue::make_integer(1));
    nested.push_back(WBE::DataValue::make_boolean(true));
    argument.set_entry("nested", nested);
    const std::array<WBE::DataValue, 1> args = {argument};

    EXPECT_EQ(backend.call("test", "echo", args), argument);
}

TEST(WBESBLuaTest, BuildsDescriptionsFromInputs) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);
    WBE::DataValue settings;
    settings.set_value("light_count", 3);
    settings.set_value("base_size", 1024);
    const std::array<WBE::DataValue, 1> args = {settings};
    const WBE::DataValue result = backend.call("test", "describe", args);

    const WBE::DataValue::List& tasks = result.find("tasks")->as_list();
    ASSERT_EQ(tasks.size(), 3U);
    EXPECT_EQ(tasks[2].get_value<std::string>("id"), "shadow_3");
    // Lua's division always produces a float; integral reads still accept the exact value.
    EXPECT_EQ(result.find("size")->get_kind(), WBE::DataValueKind::FLOAT);
    EXPECT_EQ(result.get_value<uint32_t>("size"), 512U);
}

TEST(WBESBLuaTest, InvokeConvertsReflectedTypes) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);

    const WBE::TestSerializable made = backend.invoke<WBE::TestSerializable>("test", "make_struct", int32_t{21});
    EXPECT_EQ(made.si32_test, 21);
    EXPECT_EQ(made.str_test, "from lua");
    EXPECT_EQ(made.vec3_test, glm::vec3(1.0F, 2.0F, 3.0F));

    WBE::TestSerializable source;
    source.si32_test = 5;
    source.str_test = "hi";
    const WBE::TestSerializable doubled = backend.invoke<WBE::TestSerializable>("test", "double_struct", source);
    EXPECT_EQ(doubled.si32_test, 10);
    EXPECT_EQ(doubled.str_test, "hi!");
    EXPECT_EQ(backend.invoke<int64_t>("test", "add", 40, 2), 42);
    backend.invoke<void>("test", "nothing");
}

TEST(WBESBLuaTest, RejectsInvalidModules) {
    WBE::SBLua backend;
    EXPECT_THROW(backend.load_module("", "return {}"), std::runtime_error);
    EXPECT_THROW(backend.load_module("syntax", "return {"), std::runtime_error);
    EXPECT_THROW(backend.load_module("not_table", "return 1"), std::runtime_error);
    EXPECT_THROW(backend.load_module("runtime", "error('load failure')"), std::runtime_error);
    EXPECT_FALSE(backend.has_function("syntax", "anything"));
    EXPECT_THROW(backend.call("missing", "add", {}), std::runtime_error);
}

TEST(WBESBLuaTest, FailedReloadKeepsPreviousModule) {
    WBE::SBLua backend;
    backend.load_module("test", "return { value = function() return 1 end }");
    EXPECT_THROW(backend.load_module("test", "return {"), std::runtime_error);
    EXPECT_EQ(backend.call("test", "value", {}).as_integer(), 1);
    backend.load_module("test", "return { value = function() return 2 end }");
    EXPECT_EQ(backend.call("test", "value", {}).as_integer(), 2);
}

TEST(WBESBLuaTest, ReportsScriptErrorsWithMessage) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);
    try {
        backend.call("test", "fail", {});
        FAIL() << "Expected the script error to throw.";
    } catch (const std::runtime_error& p_error) {
        EXPECT_NE(std::string(p_error.what()).find("boom"), std::string::npos);
    }
    EXPECT_THROW(backend.call("test", "missing", {}), std::runtime_error);
    // The backend stays usable after a failed call.
    EXPECT_EQ(lua_test_add(backend, 1, 1), 2);
}

TEST(WBESBLuaTest, RejectsUnrepresentableResults) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);
    try {
        backend.call("test", "return_function", {});
        FAIL() << "Expected a function result to throw.";
    } catch (const std::runtime_error& p_error) {
        EXPECT_NE(std::string(p_error.what()).find("result.callback"), std::string::npos);
    }
    EXPECT_THROW(backend.call("test", "return_mixed", {}), std::runtime_error);
    EXPECT_THROW(backend.call("test", "return_sparse", {}), std::runtime_error);
    EXPECT_THROW(backend.call("test", "return_cycle", {}), std::runtime_error);
    EXPECT_THROW(backend.call("test", "return_nan", {}), std::runtime_error);
    // A table reached twice without a cycle is copied twice.
    const WBE::DataValue shared = backend.call("test", "return_shared", {});
    EXPECT_EQ(*shared.find("first"), *shared.find("second"));
}

TEST(WBESBLuaTest, SandboxRemovesFileAndSystemAccess) {
    WBE::SBLua backend;
    backend.load_module("probe", R"(
return {
    globals = function()
        return {
            io = io == nil, os = os == nil, debug = debug == nil, package = package == nil, require = require ~= nil,
            dofile = dofile == nil, loadfile = loadfile == nil,
            string = string ~= nil, table = table ~= nil, math = math ~= nil, utf8 = utf8 ~= nil,
        }
    end,
    binary_load = function()
        local chunk = load(string.dump(function() return 1 end))
        return chunk == nil
    end,
    text_load = function()
        return load("return 7")()
    end,
}
)");
    const WBE::DataValue globals = backend.call("probe", "globals", {});
    for (const std::string& key : globals.get_all_keys()) {
        EXPECT_TRUE(globals.find(key)->as_boolean()) << key;
    }
    EXPECT_TRUE(backend.call("probe", "binary_load", {}).as_boolean());
    EXPECT_EQ(backend.call("probe", "text_load", {}).as_integer(), 7);
}

TEST(WBESBLuaTest, ModulesHaveSeparateGlobals) {
    WBE::SBLua backend;
    backend.load_module("first", "shared_name = 1\nreturn { get = function() return shared_name end }");
    backend.load_module("second", "return { get = function() return shared_name end, set = function() math = nil end }");

    EXPECT_EQ(backend.call("first", "get", {}).as_integer(), 1);
    EXPECT_TRUE(backend.call("second", "get", {}).is_nil());
    // Assigning a library name only shadows it in that module.
    backend.call("second", "set", {});
    backend.load_module("third", "return { has_math = function() return math ~= nil end }");
    EXPECT_TRUE(backend.call("third", "has_math", {}).as_boolean());
}

TEST(WBESBLuaTest, InstructionLimitStopsRunawayScripts) {
    WBE::SBLua backend(WBE::ScriptSandboxConfig{.instruction_limit = 100000});
    backend.load_module("loop", "return { spin = function() while true do end end, quick = function() return 1 end }");
    EXPECT_THROW(backend.call("loop", "spin", {}), std::runtime_error);
    // The budget restarts for each call.
    EXPECT_EQ(backend.call("loop", "quick", {}).as_integer(), 1);
    EXPECT_THROW(backend.load_module("load_loop", "while true do end"), std::runtime_error);
}

TEST(WBESBLuaTest, MemoryLimitStopsLargeAllocations) {
    WBE::SBLua backend(WBE::ScriptSandboxConfig{.memory_limit = 1U << 20U});
    backend.load_module("memory", "return { grow = function() return #string.rep('x', 1 << 24) end, small = function() return 3 end }");
    EXPECT_THROW(backend.call("memory", "grow", {}), std::runtime_error);
    EXPECT_EQ(backend.call("memory", "small", {}).as_integer(), 3);
}

TEST(WBESBLuaTest, RejectsArgumentsNestedTooDeeply) {
    WBE::SBLua backend;
    backend.load_module("test", SB_LUA_TEST_MODULE);
    WBE::DataValue deep = WBE::DataValue::make_integer(0);
    for (size_t i = 0; i <= WBE::SBLua::MAX_VALUE_DEPTH + 1; ++i) {
        WBE::DataValue wrapper;
        wrapper.push_back(std::move(deep));
        deep = std::move(wrapper);
    }
    const std::array<WBE::DataValue, 1> args = {deep};
    EXPECT_THROW(backend.call("test", "echo", args), std::runtime_error);
    EXPECT_EQ(lua_test_add(backend, 2, 2), 4);
}

/**
 * @class SBLuaTestModuleResolver
 * @brief Resolves modules from an in-memory map of names to sources, and counts the lookups.
 */
class SBLuaTestModuleResolver final : public WBE::IScriptModuleResolver {
public:
    SBLuaTestModuleResolver() = default;
    virtual ~SBLuaTestModuleResolver() override = default;
    SBLuaTestModuleResolver(const SBLuaTestModuleResolver&) = delete;
    SBLuaTestModuleResolver(SBLuaTestModuleResolver&&) noexcept = delete;
    SBLuaTestModuleResolver& operator=(const SBLuaTestModuleResolver&) = delete;
    SBLuaTestModuleResolver& operator=(SBLuaTestModuleResolver&&) noexcept = delete;

    virtual std::optional<std::string_view> try_resolve(std::string_view p_module) const noexcept override {
        ++lookup_count;
        const auto found = sources.find(std::string(p_module));
        if (found == sources.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    std::map<std::string, std::string> sources;
    mutable int lookup_count = 0;
};

TEST(WBESBLuaTest, RequireLoadsResolvedModulesOnce) {
    SBLuaTestModuleResolver resolver;
    resolver.sources["utils.math"] = R"(
local exports = {}
exports.loads = (exports.loads or 0) + 1
function exports.double(value)
    return value * 2
end
return exports
)";
    WBE::SBLua backend;
    backend.set_module_resolver(&resolver);
    backend.load_module("main", R"(
local math_utils = require("utils.math")
return {
    run = function(value)
        return math_utils.double(value)
    end,
    same = function()
        return require("utils.math") == math_utils
    end,
}
)");
    EXPECT_EQ(backend.call("main", "run", std::array<WBE::DataValue, 1>{WBE::DataValue::make_integer(21)}).as_integer(), 42);
    EXPECT_TRUE(backend.call("main", "same", {}).as_boolean());
    EXPECT_EQ(resolver.lookup_count, 1);
    // A required module is loaded under its name, like one loaded with load_module().
    EXPECT_TRUE(backend.has_function("utils.math", "double"));
}

TEST(WBESBLuaTest, RequireReturnsModulesLoadedWithoutAResolver) {
    WBE::SBLua backend;
    backend.load_module("helper", "return { value = function() return 3 end }");
    backend.load_module("main", "local helper = require('helper') return { run = function() return helper.value() end }");
    EXPECT_EQ(backend.call("main", "run", {}).as_integer(), 3);
    EXPECT_THROW(backend.load_module("other", "require('missing') return {}"), std::runtime_error);
}

TEST(WBESBLuaTest, RequireRejectsMissingCyclicAndInvalidModules) {
    SBLuaTestModuleResolver resolver;
    resolver.sources["cycle_a"] = "require('cycle_b') return {}";
    resolver.sources["cycle_b"] = "require('cycle_a') return {}";
    resolver.sources["not_a_table"] = "return 1";
    resolver.sources["failing"] = "error('broken module')";
    WBE::SBLua backend;
    backend.set_module_resolver(&resolver);
    EXPECT_THROW(backend.load_module("main", "require('missing') return {}"), std::runtime_error);
    try {
        backend.load_module("main", "require('cycle_a') return {}");
        FAIL() << "A cyclic require must fail.";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("cyclic require"), std::string::npos) << error.what();
    }
    EXPECT_THROW(backend.load_module("main", "require('not_a_table') return {}"), std::runtime_error);
    // A failed load is not cached as loading, so requiring it again reports the same failure.
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            backend.load_module("main", "require('failing') return {}");
            FAIL() << "Requiring a failing module must fail.";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("broken module"), std::string::npos) << error.what();
        }
    }
}

#endif

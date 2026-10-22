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
#ifndef WBE_FILE_I_SCRIPT_BACKEND_HH
#define WBE_FILE_I_SCRIPT_BACKEND_HH

#include "core/data/data_value.hh"
#include "core/reflection/serializable_sd.hh"
#include "core/script/i_script_module_resolver.hh"
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

namespace WhiteBirdEngine {

/**
 * @class IScriptBackend
 * @brief One isolated script runtime that loads modules and calls their functions with DataValue arguments.
 *
 * A module is a script whose top-level chunk returns a table, or the language's equivalent, of exported functions. Each
 * module runs in its own global environment, so modules of one backend cannot overwrite each other's globals. A module
 * can import another module by name through the language's import mechanism, which loads it once per backend through
 * the IScriptModuleResolver set with set_module_resolver(). Scripts cannot reach files, the operating system, or the
 * engine. Only DataValue data crosses the boundary in either direction.
 *
 * A backend is confined to the thread that uses it; its methods are not synchronized. Backends are neither copyable nor
 * movable. Script errors, values the DataValue model cannot represent, and exceeded ScriptSandboxConfig limits throw
 * std::runtime_error with the script's message or the offending value's path.
 */
class IScriptBackend {
public:
    IScriptBackend() = default;
    virtual ~IScriptBackend() = default;
    IScriptBackend(const IScriptBackend&) = delete;
    IScriptBackend(IScriptBackend&&) noexcept = delete;
    IScriptBackend& operator=(const IScriptBackend&) = delete;
    IScriptBackend& operator=(IScriptBackend&&) noexcept = delete;

    /**
     * @brief Load a module from source, replacing any module already loaded under the name.
     *
     * The module's top-level code runs immediately. A failed load leaves a previously loaded module of the same name in
     * place.
     * @param p_module Module name; non-empty.
     * @param p_source Module source code. Precompiled binary chunks are rejected.
     * @throws std::runtime_error If the name is empty, the source fails to compile or run, or it does not return a table of
     * exports.
     */
    virtual void load_module(std::string_view p_module, std::string_view p_source) = 0;

    /**
     * @brief Set the resolver that finds the modules scripts import.
     *
     * Without a resolver, only modules already loaded with load_module() can be imported.
     * @param p_resolver The resolver, or nullptr to clear it. Not owned; it must outlive its use by this backend.
     */
    virtual void set_module_resolver(const IScriptModuleResolver* p_resolver) = 0;

    /**
     * @brief Call an exported function of a loaded module.
     * @param p_module Name of a loaded module.
     * @param p_function Name of the exported function.
     * @param p_args Arguments, passed in order.
     * @return The function's first result, or nil when it returns nothing. Further results are ignored.
     * @throws std::runtime_error If the module is not loaded, the export is missing or not a function, the call fails, or
     * an argument or the result cannot be converted.
     */
    virtual DataValue call(std::string_view p_module, std::string_view p_function, std::span<const DataValue> p_args) = 0;

    /**
     * @brief Check whether a loaded module exports a function under the name.
     * @return False when the module is not loaded or the export is missing or not a function.
     */
    virtual bool has_function(std::string_view p_module, std::string_view p_function) const = 0;

    /**
     * @brief Call an exported function with reflected C++ arguments and result.
     *
     * Each argument is serialized into a DataValue and the result is deserialized into TResult, both through
     * SerializableSD. Include the generated serializer header for reflected types before instantiating this.
     * @tparam TResult Result type, or void to discard the result.
     * @throws std::runtime_error Under the conditions of call(), or if serialization or deserialization fails.
     */
    template <typename TResult, typename... TArgs>
    TResult invoke(std::string_view p_module, std::string_view p_function, const TArgs&... p_args) {
        std::array<DataValue, sizeof...(TArgs)> args{};
        [[maybe_unused]] size_t arg_index = 0; // Unused when TArgs is empty.
        (SerializableSD<TArgs>::serialize(args[arg_index++], p_args), ...);
        DataValue result = call(p_module, p_function, std::span<const DataValue>(args.data(), args.size()));
        if constexpr (!std::is_void_v<TResult>) {
            TResult typed_result{};
            SerializableSD<TResult>::deserialize(result, typed_result);
            return typed_result;
        }
    }
};

} // namespace WhiteBirdEngine

#endif

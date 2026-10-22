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
#include "platform/module/dynamic_module.hh"
#include "platform/file_system/path.hh"
#include <dlfcn.h>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {
DynamicModule::DynamicModule(const Path& p_module_path) : module_path(p_module_path) {
    auto path_str = p_module_path.string();
    handle = dlopen(path_str.c_str(), RTLD_LAZY);
    if (handle == nullptr) {
        throw std::runtime_error(std::format("Failed to open module at path: {}.", path_str));
    }
}

DynamicModule::~DynamicModule() {
    dlclose(handle);
}

void* DynamicModule::get_function_helper(std::string_view p_function_symbol) {
    auto* result = dlsym(handle, std::string(p_function_symbol).c_str());
    if (result == nullptr) {
        throw std::runtime_error(std::format(
            "Failed to load function with symobol: {} in module at path: {}", p_function_symbol, module_path.string()));
    }
    return result;
}
} // namespace WhiteBirdEngine

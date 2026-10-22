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
#include "platform/file_system/file_system.hh"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace WhiteBirdEngine {

FileSystem::FileSystem() : FileSystem(get_executable_dir()) {
}

FileSystem::~FileSystem() {
    singleton = nullptr;
}

FileSystem::FileSystem(const Path& p_root_dir) {
    singleton = this;
    root_directory = p_root_dir.lexically_normal();
    resource_directory = root_directory / "res";
    config_directory = resource_directory / "config";
}

std::string FileSystem::get_file_name(std::string_view p_path) {
    return Path(p_path).filename().generic_string();
}

Path FileSystem::get_file_dir(std::string_view p_path) {
    return Path(p_path).parent_path().lexically_normal();
}

std::string FileSystem::path_to_string(const Path& p_path) {
    return p_path.string();
}

std::string FileSystem::get_ext(const Path& p_path) {
    return p_path.extension().generic_string();
}

Path FileSystem::get_executable_dir() {
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (len == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (len == 0) {
        throw std::runtime_error("Failed to get the executable path.");
    }
    std::filesystem::path exe_path(std::wstring(buffer.data(), len));
    return exe_path.parent_path().lexically_normal();
}

} // namespace WhiteBirdEngine

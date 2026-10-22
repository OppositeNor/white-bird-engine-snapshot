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
#ifndef WBE_FILE_FILE_SYSTEM_HH
#define WBE_FILE_FILE_SYSTEM_HH

#include "platform/file_system/path.hh"
#include "utils/interface/i_singleton.hh"
#include <string>
#include <string_view>
namespace WhiteBirdEngine {

/**
 * @class FileSystem
 * @brief Platform API for file systems.
 *
 */
class FileSystem final : public ISingleton<FileSystem> {
public:
    FileSystem();
    ~FileSystem();
    FileSystem(const FileSystem&) = delete;
    FileSystem(FileSystem&&) noexcept = delete;
    FileSystem& operator=(const FileSystem&) = delete;
    FileSystem& operator=(FileSystem&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_root_dir The path of the root directory.
     */
    FileSystem(const Path& p_root_dir);

    /**
     * @brief Get the singleton.
     *
     * @return The singleton.
     */
    static FileSystem* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the directory of the executable file.
     *
     * @return The directory of the executable file.
     */
    const Path& get_root_directory() const {
        return root_directory;
    }

    /**
     * @brief Get the directory of the resource directory.
     *
     * @return The resource directory.
     */
    const Path& get_resource_directory() const {
        return resource_directory;
    }

    /**
     * @brief Get the directory of the config directory.
     *
     * @return The config directory.
     */
    const Path& get_config_directory() const {
        return config_directory;
    }

    /**
     * @brief Get the name of a file.
     *
     * @param p_path The path to the file.
     */
    static std::string get_file_name(std::string_view p_path);

    /**
     * @brief Get the directory of the file.
     *
     * @param p_path The path to the file.
     * @return The directory of the file.
     */
    static Path get_file_dir(std::string_view p_path);

    /**
     * @brief Get the path from a string.
     *
     * @param p_path The string path.
     * @return The path instance.
     */
    Path get_file_path(std::string_view p_path) const {
        return Path(p_path).lexically_normal();
    }

    /**
     * @brief Cast a path to a string.
     *
     * @param p_path The path to cast to string.
     */
    static std::string path_to_string(const Path& p_path);

    /**
     * @brief Get the extension name of the file at path.
     *
     * @param p_path The path to the file.
     */
    static std::string get_ext(const Path& p_path);

    /**
     * @brief Get the directory of the executable.
     *
     * @return The directory of the executable.
     */
    static Path get_executable_dir();

private:
    Path root_directory;
    Path resource_directory;
    Path config_directory;
    inline static FileSystem* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif

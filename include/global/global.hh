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
#ifndef WBE_FILE_GLOBAL_HH
#define WBE_FILE_GLOBAL_HH

#include "core/engine_core.hh"
#include "core/memory/unique.hh"
#include "function/engine_function.hh"
#include "platform/file_system/path.hh"
#include "resource/engine_resource.hh"
#include "utils/interface/i_singleton.hh"

namespace WhiteBirdEngine {
/**
 * @class Global
 * @brief Global class. Stores all the global objects.
 * Terminate and then reinitialize it should result in restarting the
 * game/engine.
 */
class Global final : public ISingleton<Global> {
public:
    Global() = delete;
    virtual ~Global() override;
    Global(const Global&) = delete;
    Global(Global&&) noexcept = delete;
    Global& operator=(const Global&) = delete;
    Global& operator=(Global&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_argc argc.
     * @param p_argv argv.
     */
    Global(int p_argc, char* p_argv[]);

    /**
     * @brief Constructor.
     *
     * @param p_argc argc.
     * @param p_argv argv.
     * @param p_root_dir The root directory.
     */
    Global(int p_argc, char* p_argv[], const Path& p_root_dir);

    /**
     * @brief Get the singleton.
     *
     * @return The singleton instance.
     */
    static Global* get_singleton() {
        return singleton;
    }

    /**
     * @brief The core singleton.
     */
    EngineCore* engine_core;

    /**
     * @brief The resource singleton.
     */
    Unique<EngineResource> engine_resource;

    /**
     * @brief The function singleton.
     */
    Unique<EngineFunction> engine_function;

private:
    inline static Global* singleton;
};

} // namespace WhiteBirdEngine

#endif

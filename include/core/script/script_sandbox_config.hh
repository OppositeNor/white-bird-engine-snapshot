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
#ifndef WBE_FILE_SCRIPT_SANDBOX_CONFIG_HH
#define WBE_FILE_SCRIPT_SANDBOX_CONFIG_HH

#include <cstddef>
#include <cstdint>

namespace WhiteBirdEngine {

/**
 * @class ScriptSandboxConfig
 * @brief Resource limits applied to one script backend. Copyable and movable.
 *
 * Every backend runs scripts without file, operating-system, module-loading, or engine access, regardless of this
 * configuration.
 */
struct ScriptSandboxConfig {
    /**
     * @brief Maximum number of bytes the backend's runtime may hold at once, or 0 for no limit.
     *
     * A script that exceeds it fails with an error. The limit must leave room for the runtime and its standard libraries.
     */
    size_t memory_limit = 0;
    /**
     * @brief Maximum number of script instructions one load_module() or call() may execute, or 0 for no limit.
     *
     * Backends may check the limit at a coarse granularity. A script that exceeds it fails with an error.
     */
    uint64_t instruction_limit = 0;
};

} // namespace WhiteBirdEngine

#endif

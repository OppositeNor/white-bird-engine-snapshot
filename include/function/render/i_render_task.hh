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
#ifndef WBE_FILE_I_RENDER_TASK_HH
#define WBE_FILE_I_RENDER_TASK_HH

#include "utils/utils.hh"
#include <string>

namespace WhiteBirdEngine {

using RendResTID = HashCode;
using RendTaskID = std::string;

/**
 * @class IRenderTask
 * @brief The abstract struct for render task.
 */
class IRenderTask {
public:
    IRenderTask() = default;
    virtual ~IRenderTask() = default;
    IRenderTask(const IRenderTask&) = default;
    IRenderTask(IRenderTask&&) noexcept = default;
    IRenderTask& operator=(const IRenderTask&) = default;
    IRenderTask& operator=(IRenderTask&&) noexcept = default;
};

} // namespace WhiteBirdEngine

#endif

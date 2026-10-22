/* Copyright 2026 OppositeNor

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
#include "function/render/i_renderer.hh"
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace WhiteBirdEngine {

void IRenderer::request_frame_capture(std::string_view p_path) {
    static_cast<void>(p_path);
    throw std::runtime_error("This renderer does not support frame capture.");
}

void IRenderer::drain_job_buffer_tasks() {
}

std::optional<std::future<void>> IRenderer::try_add_job(std::function<void()> p_job) {
    return add_job(std::move(p_job));
}

} // namespace WhiteBirdEngine

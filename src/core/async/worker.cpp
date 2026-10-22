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
#include "core/async/worker.hh"
#include "platform/file_system/path.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <functional>
#include <future>
#include <string>
#include <utility>

namespace WhiteBirdEngine {

Worker::Worker() : Worker(1) {
}

Worker::Worker(size_t p_thread_count) : thread_pool(p_thread_count) {
    singleton = this;
}

Worker::~Worker() {
    singleton = nullptr;
}

std::future<void> Worker::add_job(std::function<void()> p_job) {
    return thread_pool.submit_task([job = std::move(p_job)]() {
        job();
    });
}

void Worker::wait() {
    thread_pool.wait();
}

std::future<std::string> Worker::read_text_file(const Path& p_path) {
    return thread_pool.submit_task([path = p_path]() {
        return load_text_file(path.string().c_str());
    });
}

} // namespace WhiteBirdEngine

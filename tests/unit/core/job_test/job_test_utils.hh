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
#ifndef WBE_FILE_JOB_TEST_UTILS_HH
#define WBE_FILE_JOB_TEST_UTILS_HH

#include <chrono>
#include <future>

template <typename T>
inline bool is_future_ready(std::future<T>& p_future) {
    return p_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

#endif
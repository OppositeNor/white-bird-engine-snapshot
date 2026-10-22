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
#include "core/profiling/profiling_manager.hh"
#include "generated/label_manager.gen.hh"
#include "utils/utils.hh"
#include <mutex>
#include <string>
#include <utility>

namespace WhiteBirdEngine {

void ProfilingManager::push_profiling_data(ProfileData&& p_profile_data) {
    std::unique_lock lock(profile_stash_mutex);
    profile_stash[p_profile_data.channel].push_back(std::move(p_profile_data));
}

void ProfilingManager::push_profiling_data(const ProfileData& p_profile_data) {
    std::unique_lock lock(profile_stash_mutex);
    profile_stash[p_profile_data.channel].push_back(p_profile_data);
}

const std::string& ProfilingManager::get_label_name(HashCode p_label) {
    return EngineCore::get_singleton()->label_manager->get_label_name(p_label);
}
} // namespace WhiteBirdEngine

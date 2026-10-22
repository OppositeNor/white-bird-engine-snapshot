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
#ifndef WBE_FILE_PROFILLING_MANAGER_HH
#define WBE_FILE_PROFILLING_MANAGER_HH

#include "core/engine_core.hh"
#include "utils/interface/i_singleton.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class ProfilingManager
 * @brief The manager for all the profilers.
 *
 */
class ProfilingManager : public ISingleton<ProfilingManager> {
public:
    ProfilingManager() = default;
    virtual ~ProfilingManager() override = default;
    ProfilingManager(const ProfilingManager&) = delete;
    ProfilingManager(ProfilingManager&&) = delete;
    ProfilingManager& operator=(const ProfilingManager&) = delete;
    ProfilingManager& operator=(ProfilingManager&&) = delete;

    /**
     * @class ProfileData
     * @brief Data for profilations.
     *
     */
    struct ProfileData {
        ChannelID channel;
        std::string message;
        double start_time;
        double delta;
        std::string file;
        uint32_t line;

        explicit operator std::string() const {
            std::stringstream ss;
            ss << R"({"channel":")" << get_label_name(channel) << '\"' << R"(,"message":")" << message << '\"' << R"(,"start_time":)"
               << start_time << R"(,"delta":)" << delta << R"(,"file":")" << file << '\"' << R"(,"line":)" << line << R"(})";
            return ss.str();
        }
    };

    /**
     * @brief Push a profiling data to the profile stash.
     *
     * @param p_channel The channel to push the data to.
     * @param p_delta The delta time that's been pushed into the profiler in
     * seconds.
     * @param p_file The source file that the proflier is created.
     * @param p_line The line number where the profiler is created.
     */
    void push_profiling_data(const ProfileData& p_profile_data);

    /**
     * @brief Push a profiling data to the profile stash.
     *
     * @param p_channel The channel to push the data to.
     * @param p_delta The delta time that's been pushed into the profiler in
     * seconds.
     * @param p_file The source file that the proflier is created.
     * @param p_line The line number where the profiler is created.
     */
    void push_profiling_data(ProfileData&& p_profile_data);

    /**
     * @brief Get data for profiliing.
     *
     * @param p_channel The channel to get data from.
     * @return The profiling data.
     */
    const std::vector<ProfileData>& get_profile_data(ChannelID p_channel) {
        return profile_stash[p_channel];
    }

private:
    mutable std::shared_mutex profile_stash_mutex;
    mutable std::unordered_map<ChannelID, std::vector<ProfileData>> profile_stash;

    static const std::string& get_label_name(HashCode p_label);
};

} // namespace WhiteBirdEngine

#endif

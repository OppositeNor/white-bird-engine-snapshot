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

#ifndef WBE_FILE_GAME_CONFIG_OPTIONS_HH
#define WBE_FILE_GAME_CONFIG_OPTIONS_HH

#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <optional>
#include <string>

namespace WhiteBirdEngine {
/**
 * @class GameConfigOptions
 * @brief Configuration options for game.
 *
 */
struct WBE_META(WBE_CONFIG_OPTION) GameConfigOptions final : public Serializable {
    WBE_DECL_SERIALIZABLE(GameConfigOptions)
    /**
     * @brief The name of the game.
     */
    WBE_META(WBE_REFLECT)
    std::string game_name = "White Bird Engine Demo";
    /**
     * @brief Game version major.
     */
    WBE_META(WBE_REFLECT)
    uint32_t version_major = 1;
    /**
     * @brief Game version minor.
     */
    WBE_META(WBE_REFLECT)
    uint32_t version_minor = 0;
    /**
     * @brief Game version patch.
     */
    WBE_META(WBE_REFLECT)
    uint32_t version_patch = 0;
    /**
     * @brief The width of the window.
     */
    WBE_META(WBE_REFLECT)
    uint32_t window_width = 1260;
    /**
     * @brief The height of the window.
     */
    WBE_META(WBE_REFLECT)
    uint32_t window_height = 720;
    /**
     * @brief Frames in flight.
     */
    WBE_META(WBE_REFLECT)
    uint32_t frames_in_flight = 2;
    /**
     * @brief The index of the render device.
     * If not specified, white bird engine will automatically choose the most
     * optimal one.
     */
    WBE_META(WBE_REFLECT)
    std::optional<uint32_t> render_device_index;

    WBE_META(WBE_REFLECT)
    std::string render_api;

    /**
     * @brief Set limit to the rendering fps. 0 stands for unlimited.
     */
    WBE_META(WBE_REFLECT)
    int32_t limit_render_fps = 0;

    /**
     * @brief Set limit to the logic fps. 0 stands for unlimited.
     * Unlimited logic FPS is strongly unrecognized, since logic computation
     * will consume substancial amount of CPU resources.
     */
    WBE_META(WBE_REFLECT)
    int32_t limit_logic_fps = 60;

    /**
     * @brief The name of the prefered audio device. This field should be "" if
     * not specified, and the default audio device will be used.
     */
    WBE_META(WBE_REFLECT)
    std::string audio_device;

    /**
     * @brief The size of the render allocator.
     */
    WBE_META(WBE_REFLECT)
    uint64_t render_mem_pool_size = WBE_KI_B(64);
};

} // namespace WhiteBirdEngine

#endif

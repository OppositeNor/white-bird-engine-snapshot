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
#include "function/audio_ma/audio_renderer_ma.hh"
#include "core/logging/log.hh"
#include "function/engine_function.hh"
#include "function/game_config/game_config.hh"
#include "function/game_config/game_config_options.hh"
#include "utils/defs.hh"
#include <cstdint>
#include <format>
#include <stdexcept>

namespace WhiteBirdEngine {

namespace {

// Runs on miniaudio's real-time device thread: mix the engine's node graph straight into the device buffer.
void mix_engine_into_device(ma_device* p_device, void* p_output, const void* /*p_input*/, ma_uint32 p_frame_count) {
    WBE_DEBUG_ASSERT(p_device->pUserData != nullptr);
    ma_engine_read_pcm_frames(static_cast<ma_engine*>(p_device->pUserData), p_output, p_frame_count, nullptr);
}

} // namespace

AudioRendererMA::AudioRendererMA() {
    init();
}

AudioRendererMA::~AudioRendererMA() {
    terminate();
}

void AudioRendererMA::init() {
    stdout_log(WBE_CHANNEL_DEBUG)->message("Initializing miniaudio audio renderer.");
    // Initialize ma context
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        throw std::runtime_error("Failed to initialize miniaudio context");
    }
    context_initialized = true;

    // Get context devices
    ma_device_info* playback_infos;
    ma_uint32 playback_count;
    ma_device_info* capture_infos;
    ma_uint32 capture_count;
    if (ma_context_get_devices(&context, &playback_infos, &playback_count, &capture_infos, &capture_count) != MA_SUCCESS) {
        terminate();
        throw std::runtime_error("Failed to get audio device from context.");
    }

    // Get the prefered device from game config, if does not present, then use
    // default instead.
    int32_t prefered_device_index = -1;
    const auto& game_config = EngineFunction::get_singleton()->game_config->get_config_options();
    for (uint32_t i = 0; i < playback_count; ++i) {
        if ((game_config.audio_device.empty() && playback_infos[i].isDefault) ||
            game_config.audio_device == playback_infos[i].name) {
            prefered_device_index = static_cast<int32_t>(i);
            break;
        }
    }

    // Initialize minaudio device. Channels and sample rate are left at 0 so the device runs in its native format; the
    // engine adopts the device format and converts sounds into it.
    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    if (prefered_device_index != -1) {
        stdout_log(WBE_CHANNEL_DEBUG)->message(std::format("Picked audio device: {}.", playback_infos[prefered_device_index].name));
        device_config.playback.pDeviceID = &playback_infos[prefered_device_index].id;
    }
    device_config.playback.format = ma_format_f32;
    device_config.playback.channels = 0;
    device_config.sampleRate = 0;
    // An engine attached to an application-owned device does not pull audio by itself; the device callback must mix it.
    device_config.dataCallback = mix_engine_into_device;
    device_config.pUserData = &engine;
    if (ma_device_init(&context, &device_config, &device) != MA_SUCCESS) {
        terminate();
        throw std::runtime_error("Failed to initialize miniaudio device.");
    }
    device_initialized = true;

    // Initialize miniaudio engine
    ma_engine_config engine_config = ma_engine_config_init();
    engine_config.pDevice = &device;
    engine_config.pContext = &context;
    if (ma_engine_init(&engine_config, &engine) != MA_SUCCESS) {
        terminate();
        throw std::runtime_error("Failed to initialize miniaudio engine.");
    }
    engine_initialized = true;

    // The callback reads the engine, so the device may only start once the engine exists.
    if (ma_device_start(&device) != MA_SUCCESS) {
        terminate();
        throw std::runtime_error("Failed to start miniaudio device.");
    }
    stdout_log(WBE_CHANNEL_DEBUG)
        ->message(std::format("Miniaudio audio renderer initialized: {} channels at {} Hz.", device.playback.channels, device.sampleRate));
}

void AudioRendererMA::terminate() {
    // Uninitializing the device stops its callback thread, which must happen before the engine it reads is destroyed.
    if (device_initialized) {
        ma_device_uninit(&device);
        device_initialized = false;
    }
    if (engine_initialized) {
        ma_engine_uninit(&engine);
        engine_initialized = false;
    }
    if (context_initialized) {
        ma_context_uninit(&context);
        context_initialized = false;
    }
}

} // namespace WhiteBirdEngine

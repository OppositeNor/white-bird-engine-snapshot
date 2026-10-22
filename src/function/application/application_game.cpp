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
#include "function/application/application_game.hh"
#include "core/allocator/i_allocator.hh"
#include "core/async/worker.hh"
#include "core/engine_core.hh"
#include "core/job/job_buffer_ring_mpsc.hh"
#include "core/logging/log.hh"
#include "core/memory/reference_strong.hh"
#include "function/audio_ma/audio_renderer_ma.hh"
#include "function/game_config/game_config.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/input_manager_regular.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_dummy/renderer_dummy.hh"
#include "function/render_vk/renderer_vk.hh"
#include "function/window/i_window.hh"
#include "function/window/window_sdl.hh"
#include "resource/resource_manager/resource_manager.hh"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace WhiteBirdEngine {

namespace {

constexpr double LOGIC_FRAME_INTERVAL_SEC = 1.0 / 60.0;

} // namespace

ApplicationGame::ApplicationGame(RendererCapabilities p_renderer_capabilities, Ref<IInputManager> p_input_manager)
    : renderer_capabilities(p_renderer_capabilities), input_manager(std::move(p_input_manager)) {
    init();
}

void ApplicationGame::run() {
    {
        std::jthread render_thread([this] {
            run_render();
        });
        std::jthread logic_thread([this] {
            run_logic();
        });
    }
    // Run the render jobs queued after the last frame while the application objects they capture are alive.
    renderer->drain_job_buffer_tasks();
}

void ApplicationGame::init() {
    const auto& game_config = GameConfig::get_singleton()->get_config_options();
    // Initialize the deltas into a very small number.
    render_delta.store(0.0001, std::memory_order_seq_cst);
    logic_delta.store(0.0001, std::memory_order_seq_cst);

    render_job_buffer = make_ref<JobBufferRingMPSC>(global_allocator(), global_allocator(), 64);
    renderer_allocator = make_ref<RenderAllocator>(global_allocator(), game_config.render_mem_pool_size);
    if (input_manager == MEM_NULL) {
        input_manager = make_ref<InputManagerRegular>(global_allocator());
    }
    window = make_ref<WindowSDL>(global_allocator(), input_manager, get_window_properties_from_game_config_options(game_config));
    if (game_config.render_api == "vulkan") {
        renderer = make_ref<RendererVK>(global_allocator(), renderer_allocator.get(), window, render_job_buffer, renderer_capabilities);
    } else if (game_config.render_api == "dummy") {
        renderer = make_ref<RendererDummy>(global_allocator(), renderer_allocator.get(), renderer_capabilities);
    } else {
        stdout_log(WBE_CHANNEL_RENDER)
            ->warning(std::format("Unrecognized renderer: \"{}\", using a dummy renderer.", game_config.render_api));
        renderer = make_ref<RendererDummy>(global_allocator(), renderer_allocator.get(), renderer_capabilities);
    }
    worker = make_ref<Worker>(global_allocator(), 1);
    audio_renderer = make_ref<AudioRendererMA>(global_allocator());
    ResourceManager::get_singleton()->load_global_resource("res_chunks/global_resources.json");
}

void ApplicationGame::run_render() {
    bool local_should_close = false;
    auto* global_clock = EngineCore::get_singleton()->global_clock;
    double start_time, end_time;
    while (!local_should_close) {
        start_time = global_clock->get_duration();
        render_update(render_delta.load(std::memory_order_relaxed));
        window->tick_render();
        renderer->tick(render_delta.load(std::memory_order_relaxed));
        end_time = global_clock->get_duration();
        const double frame_duration = end_time - start_time;
        const DeltaSettings local_delta_settings = get_delta_settings();
        if (local_delta_settings.is_fixed) {
            if (local_delta_settings.sleep_to_delta && frame_duration < local_delta_settings.fixed_delta) {
                std::this_thread::sleep_for(std::chrono::duration<double>(local_delta_settings.fixed_delta - frame_duration));
            }
            render_delta.store(local_delta_settings.fixed_delta, std::memory_order_release);
        } else {
            render_delta.store(frame_duration, std::memory_order_release);
        }
        local_should_close = should_close.load(std::memory_order_acquire);
    }
}

void ApplicationGame::run_logic() {
    bool local_should_close = false;
    using LogicClock = std::chrono::steady_clock;
    while (!local_should_close) {
        LogicClock::time_point frame_start = LogicClock::now();
        local_should_close = window->should_close();
        should_close.store(local_should_close, std::memory_order_release);
        window->tick_logic();
        logic_update(logic_delta.load(std::memory_order_relaxed));
        const DeltaSettings local_delta_settings = get_delta_settings();
        const auto logic_frame_interval = std::chrono::duration<double>(
            local_delta_settings.is_fixed ? local_delta_settings.fixed_delta : LOGIC_FRAME_INTERVAL_SEC);
        LogicClock::time_point frame_end = LogicClock::now();
        auto frame_duration = frame_end - frame_start;
        const bool should_sleep = !local_delta_settings.is_fixed || local_delta_settings.sleep_to_delta;
        if (should_sleep && frame_duration < logic_frame_interval) {
            std::this_thread::sleep_until(
                frame_start + std::chrono::duration_cast<LogicClock::duration>(logic_frame_interval));
            frame_end = LogicClock::now();
            frame_duration = frame_end - frame_start;
        }
        if (local_delta_settings.is_fixed) {
            logic_delta.store(local_delta_settings.fixed_delta, std::memory_order_release);
        } else {
            logic_delta.store(std::chrono::duration<double>(frame_duration).count(), std::memory_order_release);
        }
    }
    worker->wait();
}

void ApplicationGame::set_delta_settings(const DeltaSettings& p_delta_settings) {
    if (p_delta_settings.is_fixed && !(p_delta_settings.fixed_delta > 0.0)) {
        throw std::runtime_error(std::format("Fixed delta must be positive, got {}.", p_delta_settings.fixed_delta));
    }
    std::lock_guard lock(delta_settings_mutex);
    delta_settings = p_delta_settings;
    if (p_delta_settings.is_fixed) {
        // The next frames must not observe a measured delta once the delta is fixed.
        render_delta.store(p_delta_settings.fixed_delta, std::memory_order_release);
        logic_delta.store(p_delta_settings.fixed_delta, std::memory_order_release);
    }
}

ApplicationGame::DeltaSettings ApplicationGame::get_delta_settings() const {
    std::lock_guard lock(delta_settings_mutex);
    return delta_settings;
}

WindowProperties ApplicationGame::get_window_properties_from_game_config_options(const GameConfigOptions& p_game_config_options) {
    WindowProperties properties{
        .title = p_game_config_options.game_name,
        .width = p_game_config_options.window_width,
        .height = p_game_config_options.window_height,
        .is_resizable = false,   // TODO
        .is_full_screen = false, // TODO
    };
    return properties;
}

} // namespace WhiteBirdEngine

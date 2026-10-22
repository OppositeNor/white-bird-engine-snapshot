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
#ifndef WBE_FILE_APPLICATION_GAME_HH
#define WBE_FILE_APPLICATION_GAME_HH

#include "core/allocator/i_allocator.hh"
#include "core/async/worker.hh"
#include "core/job/job_buffer_ring_mpsc.hh"
#include "core/memory/reference_strong.hh"
#include "function/application/i_application.hh"
#include "function/audio/audio_renderer.hh"
#include "function/game_config/game_config_options.hh"
#include "function/input_manager/i_input_manager.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/window/i_window.hh"
#include "utils/defs.hh"
#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <utility>
namespace WhiteBirdEngine {

/**
 * @class ApplicationGame
 * @brief IApplication class to run the game.
 *
 */
class ApplicationGame : public IApplication {
public:
    /**
     * @brief Initialize the game renderer with requested optional capabilities.
     *
     * @param p_renderer_capabilities Features to request before renderer creation.
     * @param p_input_manager Input manager receiving window key transitions. MEM_NULL creates an InputManagerRegular.
     * The application retains shared ownership; it must be the process's only input manager.
     */
    explicit ApplicationGame(RendererCapabilities p_renderer_capabilities = {}, Ref<IInputManager> p_input_manager = MEM_NULL);
    virtual ~ApplicationGame() = default;
    ApplicationGame(const ApplicationGame&) = delete;
    ApplicationGame(ApplicationGame&&) noexcept = delete;
    ApplicationGame& operator=(const ApplicationGame&) = delete;
    ApplicationGame& operator=(ApplicationGame&&) noexcept = delete;

    /**
     * @brief Run the render and logic threads until the window closes, then drain the renderer's queued jobs.
     *
     * The jobs run after both threads have stopped and before the application is destroyed. Afterwards the renderer refuses
     * new jobs, so add_render_job() throws.
     */
    virtual void run() override;

    virtual double get_render_delta() const override {
        return render_delta.load(std::memory_order_acquire);
    }

    virtual double get_logic_delta() const override {
        return logic_delta.load(std::memory_order_acquire);
    }

    virtual void set_delta_settings(const DeltaSettings& p_delta_settings) override;

    virtual DeltaSettings get_delta_settings() const override;

    /**
     * @brief Get the allocator for the renderer.
     *
     * @return The allocator for the renderer.
     */
    Ref<RenderAllocator> get_renderer_allocator() const {
        return renderer_allocator;
    }

    /**
     * @brief Get the window of the application.
     *
     * @return The window of the application.
     */
    Ref<IWindow> get_window() const {
        return window;
    }

    /**
     * @brief Get the renderer instance.
     *
     * @return The renderer instance.
     */
    Ref<IRenderer> get_renderer() const {
        return renderer;
    }

    /**
     * @brief Get the capabilities enabled by the renderer.
     *
     * @return The negotiated renderer capabilities.
     */
    const RendererCapabilities& get_renderer_capabilities() const {
        return renderer_capabilities;
    }

    /**
     * @brief Add a job for the renderer.
     */
    std::future<void> add_render_job(std::function<void()> p_job) {
        return renderer->add_job(std::move(p_job));
    }

private:
    Ref<RenderAllocator> renderer_allocator;
    RendererCapabilities renderer_capabilities;
    Ref<IInputManager> input_manager;
    Ref<IWindow> window;

    void init();
    void run_render();
    void run_logic();

    WBE_NO_FALSE_SHARING std::atomic<double> render_delta;
    WBE_NO_FALSE_SHARING std::atomic<double> logic_delta;
    WBE_NO_FALSE_SHARING std::atomic<bool> should_close;
    mutable std::mutex delta_settings_mutex;
    DeltaSettings delta_settings;

protected:
    Ref<JobBufferRingMPSC> render_job_buffer;
    Ref<IRenderer> renderer;
    Ref<AudioRenderer> audio_renderer;
    Ref<Worker> worker;
    WindowProperties get_window_properties_from_game_config_options(const GameConfigOptions& p_game_config_options);
};

} // namespace WhiteBirdEngine

#endif

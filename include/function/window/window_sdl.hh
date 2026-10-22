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
#ifndef WBE_FILE_WINDOW_SDL
#define WBE_FILE_WINDOW_SDL

#include "SDL_video.h"
#include "core/memory/reference_strong.hh"
#include "function/input_manager/i_input_manager.hh"
#include "i_window.hh"
#include <atomic>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class WindowSDL
 * @brief SDL Window.
 *
 */
class WindowSDL final : public IWindow {
public:
    virtual ~WindowSDL() override;
    WindowSDL(const WindowSDL&) = delete;
    WindowSDL(WindowSDL&&) noexcept = delete;
    WindowSDL& operator=(const WindowSDL&) = delete;
    WindowSDL& operator=(WindowSDL&&) noexcept = delete;

    WindowSDL(Ref<IInputManager> p_input_manager, const WindowProperties& p_window_properties);

    virtual WindowType get_window_type() const override {
        return window_type;
    }
    virtual void tick_render() override;
    virtual void tick_logic() override;
    virtual void get_size_in_pixels(uint32_t& p_width, uint32_t& p_height) override;
    virtual void resize(uint32_t p_width, uint32_t p_height) override;
    virtual bool consume_size_changed() override;
    virtual bool should_close() const override;

    virtual void init_vulkan(VkInstance p_instance) override;
    virtual VkSurfaceKHR get_vulkan_surface() const override;
    virtual std::vector<const char*> get_required_vulkan_instance_extensions() const override;

private:
    WindowType window_type = WindowType::UNKNOWN;
    WindowProperties window_properties;
    Ref<IInputManager> input_manager;

    SDL_Window* window = nullptr;
    std::atomic<bool> window_should_close = false;
    std::atomic<bool> window_size_changed = false;

    VkSurfaceKHR vulkan_surface = VK_NULL_HANDLE;
};

} // namespace WhiteBirdEngine

#endif

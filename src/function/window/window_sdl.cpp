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

#include "function/window/window_sdl.hh"
#include "SDL.h"
#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "function/input_manager/i_input_manager.hh"
#include "function/input_manager/key_codes.hh"
#include "function/window/i_window.hh"
#include <SDL_vulkan.h>
#include <atomic>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

namespace {
std::string build_sdl_error_message(std::string_view p_prefix) {
    const char* error_message = SDL_GetError();
    if (error_message == nullptr || error_message[0] == '\0') {
        return std::format("{} SDL did not report additional details.", p_prefix);
    }
    return std::format("{} SDL error: {}.", p_prefix, error_message);
}
} // namespace

WindowSDL::WindowSDL(Ref<IInputManager> p_input_manager, const WindowProperties& p_window_properties)
    : window_properties(p_window_properties), input_manager(p_input_manager) {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(std::string(window_properties.title).c_str(),
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        static_cast<int>(window_properties.width),
        static_cast<int>(window_properties.height),
        SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | (p_window_properties.is_resizable ? SDL_WINDOW_RESIZABLE : 0));
}

WindowSDL::~WindowSDL() {
    SDL_DestroyWindow(window);
    switch (window_type) {
    case WindowType::UNKNOWN:
        break;
    case WindowType::VULKAN:
        SDL_Vulkan_UnloadLibrary();
    }
    SDL_Quit();
}

void WindowSDL::tick_render() {
    // Do nothing.
}

void WindowSDL::tick_logic() { // NOLINT
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            window_should_close.store(true, std::memory_order_release);
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            window_size_changed.store(true, std::memory_order_release);
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            // Map the SDL keycode to our KeyCode
            KeyCode key_code;
            switch (event.key.keysym.sym) {
            case SDLK_0:
                key_code = KEYCODE_0;
                break;
            case SDLK_1:
                key_code = KEYCODE_1;
                break;
            case SDLK_2:
                key_code = KEYCODE_2;
                break;
            case SDLK_3:
                key_code = KEYCODE_3;
                break;
            case SDLK_4:
                key_code = KEYCODE_4;
                break;
            case SDLK_5:
                key_code = KEYCODE_5;
                break;
            case SDLK_6:
                key_code = KEYCODE_6;
                break;
            case SDLK_7:
                key_code = KEYCODE_7;
                break;
            case SDLK_8:
                key_code = KEYCODE_8;
                break;
            case SDLK_9:
                key_code = KEYCODE_9;
                break;
            case SDLK_a:
                key_code = KEYCODE_A;
                break;
            case SDLK_b:
                key_code = KEYCODE_B;
                break;
            case SDLK_c:
                key_code = KEYCODE_C;
                break;
            case SDLK_d:
                key_code = KEYCODE_D;
                break;
            case SDLK_e:
                key_code = KEYCODE_E;
                break;
            case SDLK_f:
                key_code = KEYCODE_F;
                break;
            case SDLK_g:
                key_code = KEYCODE_G;
                break;
            case SDLK_h:
                key_code = KEYCODE_H;
                break;
            case SDLK_i:
                key_code = KEYCODE_I;
                break;
            case SDLK_j:
                key_code = KEYCODE_J;
                break;
            case SDLK_k:
                key_code = KEYCODE_K;
                break;
            case SDLK_l:
                key_code = KEYCODE_L;
                break;
            case SDLK_m:
                key_code = KEYCODE_M;
                break;
            case SDLK_n:
                key_code = KEYCODE_N;
                break;
            case SDLK_o:
                key_code = KEYCODE_O;
                break;
            case SDLK_p:
                key_code = KEYCODE_P;
                break;
            case SDLK_q:
                key_code = KEYCODE_Q;
                break;
            case SDLK_r:
                key_code = KEYCODE_R;
                break;
            case SDLK_s:
                key_code = KEYCODE_S;
                break;
            case SDLK_t:
                key_code = KEYCODE_T;
                break;
            case SDLK_u:
                key_code = KEYCODE_U;
                break;
            case SDLK_v:
                key_code = KEYCODE_V;
                break;
            case SDLK_w:
                key_code = KEYCODE_W;
                break;
            case SDLK_x:
                key_code = KEYCODE_X;
                break;
            case SDLK_y:
                key_code = KEYCODE_Y;
                break;
            case SDLK_z:
                key_code = KEYCODE_Z;
                break;
            case SDLK_BACKSPACE:
                key_code = KEYCODE_BACKSPACE;
                break;
            case SDLK_TAB:
                key_code = KEYCODE_TAB;
                break;
            case SDLK_RETURN:
                key_code = KEYCODE_ENTER;
                break;
            case SDLK_SPACE:
                key_code = KEYCODE_SPACE;
                break;
            case SDLK_DELETE:
                key_code = KEYCODE_DELETE;
                break;
            case SDLK_EQUALS:
                key_code = KEYCODE_EQUAL;
                break;
            case SDLK_BACKSLASH:
                key_code = KEYCODE_BACKSLASH;
                break;
            case SDLK_UNDERSCORE:
                key_code = KEYCODE_UNDERSCORE;
                break;
            case SDLK_QUOTE:
                key_code = KEYCODE_QUOTE;
                break;
            case SDLK_BACKQUOTE:
                key_code = KEYCODE_BACKQUOTE;
                break;
            case SDLK_LEFTBRACKET:
                key_code = KEYCODE_LEFT_BRACKET;
                break;
            case SDLK_RIGHTBRACKET:
                key_code = KEYCODE_RIGHT_BRACKET;
                break;
            case SDLK_SEMICOLON:
                key_code = KEYCODE_SEMICOLON;
                break;
            case SDLK_F1:
                key_code = KEYCODE_F1;
                break;
            case SDLK_F2:
                key_code = KEYCODE_F2;
                break;
            case SDLK_F3:
                key_code = KEYCODE_F3;
                break;
            case SDLK_F4:
                key_code = KEYCODE_F4;
                break;
            case SDLK_F5:
                key_code = KEYCODE_F5;
                break;
            case SDLK_F6:
                key_code = KEYCODE_F6;
                break;
            case SDLK_F7:
                key_code = KEYCODE_F7;
                break;
            case SDLK_F8:
                key_code = KEYCODE_F8;
                break;
            case SDLK_F9:
                key_code = KEYCODE_F9;
                break;
            case SDLK_F10:
                key_code = KEYCODE_F10;
                break;
            case SDLK_F11:
                key_code = KEYCODE_F11;
                break;
            case SDLK_F12:
                key_code = KEYCODE_F12;
                break;
            case SDLK_LSHIFT:
                key_code = KEYCODE_LEFT_SHIFT;
                break;
            case SDLK_LCTRL:
                key_code = KEYCODE_LEFT_CONTROL;
                break;
            case SDLK_LALT:
                key_code = KEYCODE_LEFT_ALT;
                break;
            case SDLK_RSHIFT:
                key_code = KEYCODE_RIGHT_SHIFT;
                break;
            case SDLK_RCTRL:
                key_code = KEYCODE_RIGHT_CONTROL;
                break;
            case SDLK_RALT:
                key_code = KEYCODE_RIGHT_ALT;
                break;
            case SDLK_CAPSLOCK:
                key_code = KEYCODE_CAPSLOCK;
                break;
            case SDLK_HOME:
                key_code = KEYCODE_HOME;
                break;
            case SDLK_END:
                key_code = KEYCODE_END;
                break;
            case SDLK_PAGEUP:
                key_code = KEYCODE_PAGE_UP;
                break;
            case SDLK_PAGEDOWN:
                key_code = KEYCODE_PAGE_DOWN;
                break;
            case SDLK_INSERT:
                key_code = KEYCODE_INSERT;
                break;
            case SDLK_UP:
                key_code = KEYCODE_UP;
                break;
            case SDLK_DOWN:
                key_code = KEYCODE_DOWN;
                break;
            case SDLK_LEFT:
                key_code = KEYCODE_LEFT;
                break;
            case SDLK_RIGHT:
                key_code = KEYCODE_RIGHT;
                break;
            case SDLK_ESCAPE:
                key_code = KEYCODE_ESCAPE;
                break;
            default:
                continue;
            }
            if (event.type == SDL_KEYDOWN) {
                input_manager->set_keydown(key_code);
            } else {
                input_manager->set_keyup(key_code);
            }
        }
    }
}

bool WindowSDL::should_close() const {
    return window_should_close.load(std::memory_order_acquire);
}

void WindowSDL::get_size_in_pixels(uint32_t& p_width, uint32_t& p_height) {
    SDL_GetWindowSizeInPixels(window, reinterpret_cast<int*>(&p_width), reinterpret_cast<int*>(&p_height));
    window_properties.width = p_width;
    window_properties.height = p_height;
}

void WindowSDL::resize(uint32_t p_width, uint32_t p_height) {
    constexpr auto MAX_SIZE = static_cast<uint32_t>(std::numeric_limits<int>::max());
    if (p_width == 0) {
        throw std::runtime_error("Failed to resize SDL window: width is 0.");
    }
    if (p_height == 0) {
        throw std::runtime_error("Failed to resize SDL window: height is 0.");
    }
    if (p_width > MAX_SIZE) {
        throw std::runtime_error(std::format("Failed to resize SDL window: width {} exceeds the SDL limit {}.", p_width, MAX_SIZE));
    }
    if (p_height > MAX_SIZE) {
        throw std::runtime_error(std::format("Failed to resize SDL window: height {} exceeds the SDL limit {}.", p_height, MAX_SIZE));
    }
    SDL_SetWindowSize(window, static_cast<int>(p_width), static_cast<int>(p_height));
    // The window system may apply the size later, which SDL_WINDOWEVENT_SIZE_CHANGED reports again.
    window_size_changed.store(true, std::memory_order_release);
}

bool WindowSDL::consume_size_changed() {
    return window_size_changed.exchange(false, std::memory_order_acq_rel);
}

void WindowSDL::init_vulkan(VkInstance p_instance) {
    if (window_type != WindowType::UNKNOWN) {
        throw std::runtime_error("Failed to initialize SDL window for Vulkan: Window already initialized.");
    }
    if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
        throw std::runtime_error(build_sdl_error_message("Failed to load SDL Vulkan library."));
    }
    if (SDL_Vulkan_CreateSurface(window, p_instance, &vulkan_surface) != SDL_TRUE) {
        throw std::runtime_error(build_sdl_error_message("Failed to create Vulkan surface."));
    }
    window_type = WindowType::VULKAN;
}

VkSurfaceKHR WindowSDL::get_vulkan_surface() const {
    if (window_type != WindowType::VULKAN) {
        throw std::runtime_error(std::format("Failed to get vulkan surface: {}.",
            window_type == WindowType::UNKNOWN ? "Window not initialized" : "Not a Vulkan window."));
    }
    return vulkan_surface;
}

std::vector<const char*> WindowSDL::get_required_vulkan_instance_extensions() const {
    uint32_t extensions_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, nullptr);
    std::vector<const char*> extension_names(extensions_count);
    SDL_Vulkan_GetInstanceExtensions(window, &extensions_count, extension_names.data());
    return extension_names;
}

} // namespace WhiteBirdEngine

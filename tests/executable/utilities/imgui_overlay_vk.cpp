/* Copyright 2026 OppositeNor

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
#include "imgui_overlay_vk.hh"
#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_mouse.h"
#include "SDL_video.h"
#include "core/engine_core.hh"
#include "function/render_vk/descriptor_set_plugin_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/renderer_vk.hh"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <SDL_vulkan.h>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <format>
#include <mutex>
#include <stdexcept>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

constexpr uint32_t IMGUI_MIN_IMAGE_COUNT = 2;
constexpr float IMGUI_MIN_DELTA_TIME = 1.0e-5F;

void check_imgui_vulkan_result(VkResult p_result) {
    if (p_result < 0) {
        throw std::runtime_error(std::format("ImGuiOverlayVK: Vulkan backend call failed: {}.", string_VkResult(p_result)));
    }
}

ImGuiKey to_imgui_key(SDL_Keycode p_keycode) {
    switch (p_keycode) {
    case SDLK_TAB:
        return ImGuiKey_Tab;
    case SDLK_LEFT:
        return ImGuiKey_LeftArrow;
    case SDLK_RIGHT:
        return ImGuiKey_RightArrow;
    case SDLK_UP:
        return ImGuiKey_UpArrow;
    case SDLK_DOWN:
        return ImGuiKey_DownArrow;
    case SDLK_PAGEUP:
        return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN:
        return ImGuiKey_PageDown;
    case SDLK_HOME:
        return ImGuiKey_Home;
    case SDLK_END:
        return ImGuiKey_End;
    case SDLK_INSERT:
        return ImGuiKey_Insert;
    case SDLK_DELETE:
        return ImGuiKey_Delete;
    case SDLK_BACKSPACE:
        return ImGuiKey_Backspace;
    case SDLK_SPACE:
        return ImGuiKey_Space;
    case SDLK_RETURN:
        return ImGuiKey_Enter;
    case SDLK_KP_ENTER:
        return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE:
        return ImGuiKey_Escape;
    case SDLK_a:
        return ImGuiKey_A;
    case SDLK_c:
        return ImGuiKey_C;
    case SDLK_v:
        return ImGuiKey_V;
    case SDLK_x:
        return ImGuiKey_X;
    case SDLK_y:
        return ImGuiKey_Y;
    case SDLK_z:
        return ImGuiKey_Z;
    case SDLK_LCTRL:
        return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL:
        return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT:
        return ImGuiKey_LeftShift;
    case SDLK_RSHIFT:
        return ImGuiKey_RightShift;
    case SDLK_LALT:
        return ImGuiKey_LeftAlt;
    case SDLK_RALT:
        return ImGuiKey_RightAlt;
    case SDLK_LGUI:
        return ImGuiKey_LeftSuper;
    case SDLK_RGUI:
        return ImGuiKey_RightSuper;
    default:
        return ImGuiKey_None;
    }
}

// Scale window coordinates of mouse events to the drawable pixels the overlay renders in.
void scale_mouse_position(SDL_Window* p_window, int32_t& p_x, int32_t& p_y) {
    if (p_window == nullptr) {
        return;
    }
    int window_width = 0;
    int window_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GetWindowSize(p_window, &window_width, &window_height);
    SDL_Vulkan_GetDrawableSize(p_window, &drawable_width, &drawable_height);
    if (window_width <= 0 || window_height <= 0) {
        return;
    }
    p_x = p_x * drawable_width / window_width;
    p_y = p_y * drawable_height / window_height;
}

} // namespace

ImGuiOverlayVK::ImGuiOverlayVK(RendererVK* p_renderer)
    : render_data(p_renderer->get_render_context()), color_format(render_data->swap_chain_format),
      pending_events(global_allocator()), frame_events(global_allocator()) {
    auto* descriptor_set_plugin = DescriptorSetPluginVK::get_singleton();
    if (descriptor_set_plugin == nullptr) {
        throw std::runtime_error("ImGuiOverlayVK: the renderer must be created with RendererCapabilities::descriptor_set.");
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        throw std::runtime_error("ImGuiOverlayVK: another Dear ImGui context already exists.");
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Executable tests do not persist window layouts.
    io.IniFilename = nullptr;
    io.BackendPlatformName = "wbe_executable_test_sdl_events";
    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = RendererVK::VULKAN_API_VERSION;
    init_info.Instance = descriptor_set_plugin->get_instance();
    init_info.PhysicalDevice = render_data->physical_device;
    init_info.Device = render_data->logic_device;
    init_info.QueueFamily = render_data->graphics_queue_family_index;
    init_info.Queue = render_data->graphics_queue;
    init_info.DescriptorPool = descriptor_set_plugin->get_descriptor_pool();
    init_info.MinImageCount = IMGUI_MIN_IMAGE_COUNT;
    // The backend cycles its vertex buffers over ImageCount frames, so it must cover every frame in flight.
    init_info.ImageCount = std::max({IMGUI_MIN_IMAGE_COUNT, render_data->swap_chain_images_count, render_data->frames_in_flight});
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &color_format;
    init_info.Allocator = render_data->allocation_callbacks;
    init_info.CheckVkResultFn = check_imgui_vulkan_result;
    if (!ImGui_ImplVulkan_Init(&init_info)) {
        ImGui::DestroyContext();
        throw std::runtime_error("ImGuiOverlayVK: failed to initialize the Dear ImGui Vulkan backend.");
    }
    SDL_AddEventWatch(&ImGuiOverlayVK::on_sdl_event, this);
}

ImGuiOverlayVK::~ImGuiOverlayVK() {
    SDL_DelEventWatch(&ImGuiOverlayVK::on_sdl_event, this);
    // The backend destroys buffers and textures that recorded frames may still use.
    vkDeviceWaitIdle(render_data->logic_device);
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiOverlayVK::begin_frame(double p_delta) {
    {
        std::lock_guard lock(event_mutex);
        frame_events.swap(pending_events);
    }
    for (const SDL_Event& event : frame_events) {
        process_event(event);
    }
    frame_events.clear();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(render_data->swap_chain_extent.width),
        static_cast<float>(render_data->swap_chain_extent.height));
    io.DeltaTime = std::max(static_cast<float>(p_delta), IMGUI_MIN_DELTA_TIME);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void ImGuiOverlayVK::end_frame() {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    // Texture uploads submit and wait on the graphics queue, so they run here instead of while a task is recorded.
    if (draw_data->Textures != nullptr) {
        for (ImTextureData* texture : *draw_data->Textures) {
            if (texture->Status != ImTextureStatus_OK) {
                ImGui_ImplVulkan_UpdateTexture(texture);
            }
        }
        draw_data->Textures = nullptr;
    }
}

void ImGuiOverlayVK::record(VkCommandBuffer p_command_buffer) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr || !draw_data->Valid) {
        return;
    }
    ImGui_ImplVulkan_RenderDrawData(draw_data, p_command_buffer);
}

int ImGuiOverlayVK::on_sdl_event(void* p_user_data, SDL_Event* p_event) {
    auto* overlay = static_cast<ImGuiOverlayVK*>(p_user_data);
    SDL_Event event = *p_event;
    if (event.type == SDL_MOUSEMOTION) {
        scale_mouse_position(SDL_GetWindowFromID(event.motion.windowID), event.motion.x, event.motion.y);
    } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        scale_mouse_position(SDL_GetWindowFromID(event.button.windowID), event.button.x, event.button.y);
    }
    std::lock_guard lock(overlay->event_mutex);
    overlay->pending_events.push_back(event);
    // The return value of an event watch is ignored.
    return 0;
}

void ImGuiOverlayVK::process_event(const SDL_Event& p_event) {
    ImGuiIO& io = ImGui::GetIO();
    switch (p_event.type) {
    case SDL_MOUSEMOTION:
        io.AddMousePosEvent(static_cast<float>(p_event.motion.x), static_cast<float>(p_event.motion.y));
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int button = -1;
        switch (p_event.button.button) {
        case SDL_BUTTON_LEFT:
            button = ImGuiMouseButton_Left;
            break;
        case SDL_BUTTON_RIGHT:
            button = ImGuiMouseButton_Right;
            break;
        case SDL_BUTTON_MIDDLE:
            button = ImGuiMouseButton_Middle;
            break;
        default:
            break;
        }
        if (button >= 0) {
            io.AddMousePosEvent(static_cast<float>(p_event.button.x), static_cast<float>(p_event.button.y));
            io.AddMouseButtonEvent(button, p_event.type == SDL_MOUSEBUTTONDOWN);
        }
        break;
    }
    case SDL_MOUSEWHEEL:
        io.AddMouseWheelEvent(-p_event.wheel.preciseX, p_event.wheel.preciseY);
        break;
    case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(p_event.text.text);
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        const bool down = p_event.type == SDL_KEYDOWN;
        const uint16_t modifiers = p_event.key.keysym.mod;
        io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (modifiers & KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (modifiers & KMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (modifiers & KMOD_GUI) != 0);
        if (const ImGuiKey key = to_imgui_key(p_event.key.keysym.sym); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        break;
    }
    case SDL_WINDOWEVENT:
        if (p_event.window.event == SDL_WINDOWEVENT_LEAVE) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        } else if (p_event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            io.AddFocusEvent(true);
        } else if (p_event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            io.AddFocusEvent(false);
        }
        break;
    default:
        break;
    }
}

} // namespace WhiteBirdEngine

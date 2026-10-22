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
#ifndef WBE_FILE_I_WINDOW_HH
#define WBE_FILE_I_WINDOW_HH

#include <cstdint>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

/**
 * @class WindowProperties
 * @brief Properties of a window.
 *
 */
struct WindowProperties {
    /**
     * @brief The title of the window.
     */
    std::string_view title;
    /**
     * @brief The width of the window.
     */
    uint32_t width;
    /**
     * @brief The height of the window.
     */
    uint32_t height;
    /**
     * @brief Is the window resizable.
     */
    bool is_resizable;
    /**
     * @brief Is window full screen.
     */
    bool is_full_screen;
};

/**
 * @brief Types of windows.
 */
enum class WindowType {
    UNKNOWN,
    VULKAN,
};

/**
 * @class IWindow
 * @brief Window interface.
 *
 */
class IWindow {
public:
    IWindow() = default;
    virtual ~IWindow() = default;
    IWindow(const IWindow&) = delete;
    IWindow(IWindow&&) noexcept = delete;
    IWindow& operator=(const IWindow&) = delete;
    IWindow& operator=(IWindow&&) noexcept = delete;

    /**
     * @brief Get the type of the window.
     *
     * @return The type of the window.
     */
    virtual WindowType get_window_type() const = 0;

    /**
     * @brief Called every render tick.
     */
    virtual void tick_render() = 0;

    /**
     * @brief Called every logic tick.
     */
    virtual void tick_logic() = 0;

    /**
     * @brief Should close the window.
     *
     * @return True if the window should be closed. False otherwise.
     */
    virtual bool should_close() const = 0;

    /**
     * @brief Get the window size in pixels.
     *
     * @param p_width The width of the window.
     * @param p_height The height of the window.
     */
    virtual void get_size_in_pixels(uint32_t& p_width, uint32_t& p_height) = 0;

    /**
     * @brief Resize the window.
     *
     * The window system may apply the new size asynchronously. Once the size changes, consume_size_changed() reports
     * it, so that the renderer resizes its swap chain to the new size. Must be called from the logic thread, or before
     * the application starts its threads.
     *
     * @param p_width The new width of the window, in screen coordinates.
     * @param p_height The new height of the window, in screen coordinates.
     * @throws std::runtime_error If p_width or p_height is 0 or does not fit the window system's size range.
     */
    virtual void resize(uint32_t p_width, uint32_t p_height) = 0;

    /**
     * @brief Report whether the size of the window changed since the last call, and clear the report.
     *
     * Both resize() and size changes made through the window system, such as the user resizing the window, are reported.
     * Thread-safe.
     *
     * @return True if the size of the window changed since the last call. False otherwise.
     */
    virtual bool consume_size_changed() = 0;

    /**
     * @brief Initialize Vulkan window.
     *
     * @param p_instance The Vulkan instance.
     */
    virtual void init_vulkan(VkInstance p_instance) = 0;

    /**
     * @brief Get the Vulkan surface instance of this window.
     *
     * @return The Vulkan surface instance.
     */
    virtual VkSurfaceKHR get_vulkan_surface() const = 0;

    /**
     * @brief Get the required Vulkan extensions.
     *
     * @return The required Vulkan
     */
    virtual std::vector<const char*> get_required_vulkan_instance_extensions() const = 0;
};

} // namespace WhiteBirdEngine

#endif

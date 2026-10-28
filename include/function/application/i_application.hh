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
#ifndef WBE_FILE_APPLICATION_HH
#define WBE_FILE_APPLICATION_HH

namespace WhiteBirdEngine {

/**
 * @class IApplication
 * @brief IApplication class.
 *
 */
class IApplication {
public:
    /**
     * @struct DeltaSettings
     * @brief Controls the delta time an application reports to its render and logic updates.
     *
     * Trivially copyable value type.
     */
    struct DeltaSettings {
        /**
         * @brief Whether every render and logic update receives fixed_delta instead of the measured frame duration.
         *
         * While fixed, frames shorter than fixed_delta are extended to fixed_delta when sleep_to_delta is set. Frames that
         * already exceed it are not extended, but still report fixed_delta.
         */
        bool is_fixed = false;

        /**
         * @brief Whether the frame freezer sleeps so that fixed frames last at least fixed_delta.
         *
         * Only used while is_fixed is set. When cleared, fixed render and logic frames run without sleeping, including the
         * logic thread's regular frame limit, while still reporting fixed_delta.
         */
        bool sleep_to_delta = true;

        /**
         * @brief The delta time in seconds reported while is_fixed is set. Must be positive when is_fixed is set.
         */
        double fixed_delta = 0.0;
    };

    IApplication() = default;
    virtual ~IApplication() = default;
    IApplication(const IApplication&) = default;
    IApplication(IApplication&&) noexcept = default;
    IApplication& operator=(const IApplication&) = default;
    IApplication& operator=(IApplication&&) noexcept = default;

    /**
     * @brief Run the application.
     */
    virtual void run() = 0;

    /**
     * @brief Get the difference between pair of render frames.
     *
     * @return The difference between pair of render frames.
     */
    virtual double get_render_delta() const = 0;

    /**
     * @brief Get the difference between pair of logic frames.
     *
     * @return The difference between pair of logic frames.
     */
    virtual double get_logic_delta() const = 0;

    /**
     * @brief Set how the render and logic deltas are produced.
     *
     * Thread-safe. Takes effect from the next render and logic frame.
     *
     * @param p_delta_settings The new delta settings.
     * @throws std::runtime_error If p_delta_settings is fixed with a non-positive fixed_delta.
     */
    virtual void set_delta_settings(const DeltaSettings& p_delta_settings) = 0;

    /**
     * @brief Get the current delta settings.
     *
     * Thread-safe.
     *
     * @return The current delta settings.
     */
    virtual DeltaSettings get_delta_settings() const = 0;

    /**
     * @brief The function called every render frame.
     *
     * @param p_delta The delta time since last render frame.
     */
    virtual void render_update(double p_delta) {};

    /**
     * @brief The function called every logic frame.
     *
     * @param p_delta The delta time since last render frame.
     */
    virtual void logic_update(double p_delta) {};

protected:
};

} // namespace WhiteBirdEngine

#endif

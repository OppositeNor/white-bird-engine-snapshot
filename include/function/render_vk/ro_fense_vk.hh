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
#ifndef WBE_FILE_RO_FENSE_VK_HH
#define WBE_FILE_RO_FENSE_VK_HH

#include "function/render_vk/render_context_vk.hh"
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class ROFenseVK
 * @brief RAII wrapper for a Vulkan fence used to observe GPU-job and frame-in-flight completion.
 *
 * Waiting is thread-safe and may be performed by an engine worker. The fence must not outlive the renderer that created it.
 * Copying and moving are disabled; share ownership with Ref.
 */
class ROFenseVK final {
public:
    ROFenseVK() = delete;
    ~ROFenseVK();
    ROFenseVK(const ROFenseVK&) = delete;
    ROFenseVK(ROFenseVK&&) noexcept = delete;
    ROFenseVK& operator=(const ROFenseVK&) = delete;
    ROFenseVK& operator=(ROFenseVK&&) noexcept = delete;

    /**
     * @brief Create a fence.
     * @param p_render_data Renderer data that owns the Vulkan device and delayed-destruction queue.
     * @param p_signaled Whether the fence starts in the signaled state.
     */
    explicit ROFenseVK(const RenderContextVK* p_render_data, bool p_signaled = false);

    /**
     * @brief Wait indefinitely for the submitted GPU job to finish.
     * @throws std::runtime_error if Vulkan cannot complete the wait.
     */
    void wait() const;

    /**
     * @brief Return the fence to the unsignaled state.
     *
     * The fence must not be pending in a queue submission.
     *
     * @throws std::runtime_error if Vulkan cannot reset the fence.
     */
    void reset();

    /**
     * @brief Get the wrapped Vulkan fence for queue submission.
     * @return The wrapped fence.
     */
    VkFence get_fense() const {
        return fense;
    }

private:
    const RenderContextVK* render_data = nullptr;
    VkFence fense = VK_NULL_HANDLE;
};

} // namespace WhiteBirdEngine

#endif

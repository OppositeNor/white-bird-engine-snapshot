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
#ifndef WBE_FILE_RO_RAY_TRACING_VK
#define WBE_FILE_RO_RAY_TRACING_VK

#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <stdexcept>
namespace WhiteBirdEngine {
class IRORayTracingVK : public IRenderObjectVK {
public:
    IRORayTracingVK() = delete;
    virtual ~IRORayTracingVK() override = default;
    IRORayTracingVK(const IRORayTracingVK&) = delete;
    IRORayTracingVK(IRORayTracingVK&&) noexcept = delete;
    IRORayTracingVK& operator=(const IRORayTracingVK&) = delete;
    IRORayTracingVK& operator=(IRORayTracingVK&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_render_data The base render data.
     */
    IRORayTracingVK(const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data)
        : IRenderObjectVK(p_render_data), ray_tracing_data(p_ray_tracing_data) {
        if (p_ray_tracing_data == nullptr) {
            throw std::runtime_error("Ray tracing data is nullptr.");
        }
    }

protected:
    const BaseRayTracingDataVK* ray_tracing_data;
};

} // namespace WhiteBirdEngine

#endif

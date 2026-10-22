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
#ifndef WBE_FILE_I_RENDER_OBJECT_VK_HH
#define WBE_FILE_I_RENDER_OBJECT_VK_HH

#include "core/core_utils.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <stdexcept>
#include <vulkan/vulkan_core.h>
namespace WhiteBirdEngine {

/**
 * @class IRenderObjectVK
 * @brief Vulkan render object.
 *
 * All methods are confined to the rendering thread and do not provide internal synchronization. Callers on other threads
 * must submit operations through the renderer job buffer. Objects created with make_ref() know their owning Ref, which
 * bindable render objects use to import themselves into render graphs.
 */
class IRenderObjectVK : public IRenderObject, public ThisRef<IRenderObjectVK> {
public:
    IRenderObjectVK() = delete;
    virtual ~IRenderObjectVK() override = default;
    IRenderObjectVK(const IRenderObjectVK&) = delete;
    IRenderObjectVK(IRenderObjectVK&&) noexcept = delete;
    IRenderObjectVK& operator=(const IRenderObjectVK&) = delete;
    IRenderObjectVK& operator=(IRenderObjectVK&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_render_data The base render data.
     */
    IRenderObjectVK(const RenderContextVK* p_render_data) : render_data(p_render_data) {
        if (p_render_data == nullptr) {
            throw std::runtime_error("Render data is nullptr.");
        }
    }

    /**
     * @brief Return structured descriptor-debug rows owned by this render object.
     *
     * Implementations must report only descriptors that already exist and must not allocate new descriptors while inspecting.
     *
     * @return Descriptor-debug rows for this object.
     */
    virtual DebugDataRows descriptor_debug_data() const {
        return DebugDataRows(render_data->allocator);
    }

protected:
    /**
     * @brief The base render data.
     */
    const RenderContextVK* render_data;
};

} // namespace WhiteBirdEngine

#endif

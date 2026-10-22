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
#ifndef WBE_FILE_RENDER_GRAPH_CONTEXT_VK_HH
#define WBE_FILE_RENDER_GRAPH_CONTEXT_VK_HH

#include "function/render_vk/render_context_vk.hh"
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @class RendGraphContextVK
 * @brief The renderer state that render graph resource and task factories are evaluated against.
 *
 * Render graphs re-evaluate their factories whenever the context changes, e.g. after the swap chain is resized: resources
 * whose configuration changed are reallocated and every task is recreated. Copyable and movable.
 */
struct RendGraphContextVK {
    /** The extent of the swap chain images. */
    VkExtent2D swap_chain_extent{};

    /**
     * @brief Build the context of the current renderer state.
     * @param p_render_data The base render data.
     * @return The context describing p_render_data.
     */
    static RendGraphContextVK from_render_data(const RenderContextVK& p_render_data);

    /**
     * @brief Compare two contexts.
     * @param p_other The context to compare with.
     * @return True if every field of both contexts is equal.
     */
    bool operator==(const RendGraphContextVK& p_other) const;
};

} // namespace WhiteBirdEngine

#endif

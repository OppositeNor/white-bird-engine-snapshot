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
#ifndef WBE_FILE_I_RENDER_OBJECT_CONTAINER_HH
#define WBE_FILE_I_RENDER_OBJECT_CONTAINER_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/i_render_task.hh"
#include <cstdint>
#include <stdexcept>

namespace WhiteBirdEngine {

/**
 * @class IRenderObjectContainer
 * @brief Interface for stable resource bindings that resolve one render object at use time.
 */
class IRenderObjectContainer {
public:
    IRenderObjectContainer() = default;
    virtual ~IRenderObjectContainer() = default;
    IRenderObjectContainer(const IRenderObjectContainer&) = delete;
    IRenderObjectContainer(IRenderObjectContainer&&) noexcept = delete;
    IRenderObjectContainer& operator=(const IRenderObjectContainer&) = delete;
    IRenderObjectContainer& operator=(IRenderObjectContainer&&) noexcept = delete;

    virtual Ref<IRenderObject> get() = 0;
    virtual Ref<IRenderObject> get() const = 0;
    virtual Ref<IRenderObject> get(uint32_t p_frame_slot) = 0;
    virtual Ref<IRenderObject> get(uint32_t p_frame_slot) const = 0;

    RendResTID get_resource_type_id() const {
        return resource_type_id;
    }

protected:
    void retain_resource_type(Ref<IRenderObject> p_resource) {
        if (p_resource == MEM_NULL) {
            return;
        }
        RendResTID concrete_type_id = p_resource->get_type_uuid();
        if (resource_type_id == 0) {
            resource_type_id = concrete_type_id;
            return;
        }
        if (resource_type_id != concrete_type_id) {
            throw std::runtime_error("Render object container resources must have the same concrete type.");
        }
    }

    RendResTID resource_type_id = 0;
};

} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_ROC_SINGLE_VK_HH
#define WBE_FILE_ROC_SINGLE_VK_HH

#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_object_container_vk.hh"
#include <cstdint>

namespace WhiteBirdEngine {

/**
 * @class ROCSingleVK
 * @brief Container that resolves the same render object for every slot.
 */
class ROCSingleVK final : public RenderObjectContainerVK {
public:
    ROCSingleVK() = delete;
    virtual ~ROCSingleVK() override = default;
    ROCSingleVK(const ROCSingleVK&) = delete;
    ROCSingleVK(ROCSingleVK&&) noexcept = delete;
    ROCSingleVK& operator=(const ROCSingleVK&) = delete;
    ROCSingleVK& operator=(ROCSingleVK&&) noexcept = delete;

    /**
     * @brief Create the render object with a factory.
     * @param p_factory Factory called with slot 0 now and on every recreate().
     * @throws std::runtime_error If p_factory is null or returns null.
     */
    explicit ROCSingleVK(Factory p_factory);

    virtual void set(uint32_t p_frame_slot, Ref<IRenderObjectVK> p_resource) override;

    virtual void clear(uint32_t p_frame_slot) override;

    virtual void clear() override;

    void set(Ref<IRenderObjectVK> p_resource);

    virtual Ref<IRenderObject> get() override;
    virtual Ref<IRenderObject> get() const override;
    virtual Ref<IRenderObject> get(uint32_t p_frame_slot) override;
    virtual Ref<IRenderObject> get(uint32_t p_frame_slot) const override;
    virtual DebugDataRows descriptor_debug_data() const override;
    virtual void recreate() override;

private:
    Factory factory;
    Ref<IRenderObjectVK> resource;
};

} // namespace WhiteBirdEngine
#endif
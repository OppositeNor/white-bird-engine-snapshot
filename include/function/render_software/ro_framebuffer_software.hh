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
#ifndef WBE_FILE_RO_FRAMEBUFFER_SOFTWARE_HH
#define WBE_FILE_RO_FRAMEBUFFER_SOFTWARE_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/i_render_object.hh"
#include "function/render_software/ro_image_software.hh"
#include "utils/utils.hh"

namespace WhiteBirdEngine {
/**
 * @class ROFramebufferSoftware
 * @brief Framebuffer.
 */
class WBE_META(WBE_RENDER_OBJECT) ROFramebufferSoftware final : public IRenderObject {
public:
    ROFramebufferSoftware() = delete;
    virtual ~ROFramebufferSoftware() override = default;
    ROFramebufferSoftware(const ROFramebufferSoftware&) = delete;
    ROFramebufferSoftware(ROFramebufferSoftware&&) = delete;
    ROFramebufferSoftware& operator=(const ROFramebufferSoftware&) = delete;
    ROFramebufferSoftware& operator=(ROFramebufferSoftware&&) = delete;

    virtual TypeID get_type_uuid() const override;

    ROFramebufferSoftware(Ref<ROImageSoftware> p_image);

    // TODO
private:
    Ref<ROImageSoftware> image = MEM_NULL;
};
} // namespace WhiteBirdEngine

#endif

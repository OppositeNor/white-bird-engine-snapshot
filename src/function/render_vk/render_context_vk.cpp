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
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_sampler_vk.hh"
#include "function/render_vk/ro_staging_buffer_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include <stdexcept>

namespace WhiteBirdEngine {

RenderContextVK::RenderContextVK(RenderAllocator* p_allocator) : allocator(p_allocator), queue_family_indices(p_allocator) {
    if (p_allocator == nullptr) {
        throw std::runtime_error("Failed to construct render context: allocator is null.");
    }
}

Ref<ROBufferVK> RenderContextVK::create_buffer(const BufferDescVK& p_description) const {
    return make_ref<ROBufferVK>(allocator, this, p_description);
}

Ref<ROBufferVK> RenderContextVK::create_buffer(const BufferAliasDescVK& p_description) const {
    return make_ref<ROBufferVK>(allocator, this, p_description);
}

Ref<ROImageVK> RenderContextVK::create_image(const ImageDescVK& p_description) const {
    return make_ref<ROImageVK>(allocator, this, p_description);
}

Ref<ROImageVK> RenderContextVK::create_image(const ImageAliasDescVK& p_description) const {
    return make_ref<ROImageVK>(allocator, this, p_description);
}

Ref<ROImageViewVK> RenderContextVK::create_image_view(const ImageViewDescVK& p_description) const {
    return make_ref<ROImageViewVK>(allocator, this, p_description);
}

Ref<ROSamplerVK> RenderContextVK::create_sampler(const SamplerDescVK& p_description) const {
    return make_ref<ROSamplerVK>(allocator, this, p_description);
}

Ref<ROStagingBufferVK> RenderContextVK::create_staging_buffer(const StagingBufferDescVK& p_description) const {
    return make_ref<ROStagingBufferVK>(allocator, this, p_description);
}

Ref<ROShaderVK> RenderContextVK::create_shader(const ShaderDescVK& p_description) const {
    return make_ref<ROShaderVK>(allocator, this, p_description);
}

Ref<ROGraphicsPipelineVK> RenderContextVK::create_graphics_pipeline(const GraphicsPipelineDescVK& p_description) const {
    return make_ref<ROGraphicsPipelineVK>(allocator, this, p_description);
}

Ref<ROComputePipelineVK> RenderContextVK::create_compute_pipeline(const ComputePipelineDescVK& p_description) const {
    return make_ref<ROComputePipelineVK>(allocator, this, p_description);
}

} // namespace WhiteBirdEngine

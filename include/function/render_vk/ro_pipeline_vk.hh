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
#ifndef WBE_FILE_RO_PIPELINE_VK_HH
#define WBE_FILE_RO_PIPELINE_VK_HH

#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class GraphicsPipelineCompilerVK;
class ComputePipelineCompilerVK;

/**
 * @class ROPipelineVK
 * @brief Shared Vulkan pipeline runtime ownership.
 */
class ROPipelineVK : public IRenderObjectVK {
public:
    ROPipelineVK() = delete;
    virtual ~ROPipelineVK() override;
    ROPipelineVK(const ROPipelineVK&) = delete;
    ROPipelineVK(ROPipelineVK&&) noexcept = delete;
    ROPipelineVK& operator=(const ROPipelineVK&) = delete;
    ROPipelineVK& operator=(ROPipelineVK&&) noexcept = delete;

    /**
     * @brief Return the owned Vulkan pipeline handle.
     * @return Pipeline handle.
     */
    VkPipeline get_pipeline() const {
        return pipeline;
    }

    /**
     * @brief Return the immutable reflected descriptor layout retained by this pipeline.
     * @return Descriptor layout used by task-owned binding state.
     */
    const ShaderDescriptorHeapLayoutVK& get_descriptor_layout() const {
        return descriptor_layout;
    }

protected:
    /**
     * @brief Construct shared runtime state.
     * @param p_render_data Base Vulkan render data.
     */
    explicit ROPipelineVK(const RenderContextVK* p_render_data);

    /**
     * @brief Publish the created Vulkan pipeline handle to the shared owner.
     * @param p_pipeline Created pipeline handle.
     */
    void set_pipeline(VkPipeline p_pipeline) {
        pipeline = p_pipeline;
    }

private:
    friend class PipelineCompilerVK;
    friend class GraphicsPipelineCompilerVK;
    friend class ComputePipelineCompilerVK;

    void set_descriptor_layout(const ShaderDescriptorHeapLayoutVK& p_layout);
    VkPipeline pipeline = VK_NULL_HANDLE;
    ShaderDescriptorHeapLayoutVK descriptor_layout;
};

} // namespace WhiteBirdEngine

#endif

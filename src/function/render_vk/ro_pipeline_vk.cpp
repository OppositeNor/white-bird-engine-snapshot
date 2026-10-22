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
#include "function/render_vk/ro_pipeline_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include <stdexcept>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

ROPipelineVK::ROPipelineVK(const RenderContextVK* p_render_data) : IRenderObjectVK(p_render_data) {
}

ROPipelineVK::~ROPipelineVK() {
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }
    render_data->register_delayed_call(
        [render_data = render_data, pipeline = pipeline]() {
            vkDestroyPipeline(render_data->logic_device, pipeline, render_data->allocation_callbacks);
        },
        render_data->frames_in_flight);
}

void ROPipelineVK::set_descriptor_layout(const ShaderDescriptorHeapLayoutVK& p_layout) {
    if (p_layout.size > render_data->descriptor_heap_properties.maxPushDataSize) {
        throw std::runtime_error("Failed to set pipeline descriptor layout: reflected push data exceeds the device limit.");
    }
    descriptor_layout = p_layout;
}

} // namespace WhiteBirdEngine

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
#ifndef WBE_FILE_RENDER_TASK_CREATE_UTILS_VK_HH
#define WBE_FILE_RENDER_TASK_CREATE_UTILS_VK_HH

#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @brief Get a compiled compute pipeline for a render task's create().
 * @param p_render_data Render data whose RenderDatabase holds the pipeline.
 * @param p_task_name Task class name used in error messages.
 * @param p_pipeline_key Resource key the pipeline is compiled under.
 * @throws std::runtime_error If the render data, its allocator, or its RenderDatabase is unavailable, or no compute pipeline
 * is compiled under p_pipeline_key.
 */
Ref<ROComputePipelineVK> acquire_task_compute_pipeline_vk(
    const RenderContextVK* p_render_data, std::string_view p_task_name, std::string_view p_pipeline_key);

/**
 * @brief Get a compiled graphics pipeline for a render task's create().
 * @param p_render_data Render data whose RenderDatabase holds the pipeline.
 * @param p_task_name Task class name used in error messages.
 * @param p_pipeline_key Resource key the pipeline is compiled under.
 * @throws std::runtime_error If the render data, its allocator, or its RenderDatabase is unavailable, or no graphics
 * pipeline is compiled under p_pipeline_key.
 */
Ref<ROGraphicsPipelineVK> acquire_task_graphics_pipeline_vk(
    const RenderContextVK* p_render_data, std::string_view p_task_name, std::string_view p_pipeline_key);

/**
 * @brief One custom push-constant value resolved from recipe parameters.
 */
struct TaskPushConstantVK {
    /** Custom WBE_PUSH_CONSTANT identifier. */
    std::string name;
    /** The value, typed as the pipelines declare the identifier. */
    PushConstantValueVK value;
};

/**
 * @brief Resolve recipe push constants, a map from custom WBE_PUSH_CONSTANT identifier to number, for a task's create().
 *
 * Each value is converted to the scalar type the first of p_layouts that declares the identifier reflects: integers must be
 * in range for `uint` and `int` fields, and integer-valued floats are accepted for them.
 *
 * @param p_push_constants The recipe's `push_constants` value; nil yields no values.
 * @param p_layouts The reflected layouts of the pipelines the task records (ROPipelineVK::get_descriptor_layout()); null
 * entries are skipped.
 * @param p_task_name Task class name used in error messages.
 * @throws std::runtime_error If p_push_constants is neither nil nor a map, no layout declares an identifier, or a value
 * does not convert to the declared type.
 */
std::vector<TaskPushConstantVK> resolve_task_push_constants_vk(
    const DataValue& p_push_constants, std::span<const ShaderDescriptorHeapLayoutVK* const> p_layouts, std::string_view p_task_name);

} // namespace WhiteBirdEngine

#endif

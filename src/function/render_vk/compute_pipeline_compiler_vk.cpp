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
#include "function/render_vk/compute_pipeline_compiler_vk.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_loader.hh"
#include "function/render/shader_metadata_query.hh"
#include "function/render_vk/pipeline_compiler_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <stdexcept>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

ComputePipelineCompilerVK::ComputePipelineCompilerVK(const ComputePipelineCompilerVKContext& p_context)
    : PipelineCompilerVK(
          PipelineCompilerVKContext{
              .allocator = p_context.allocator,
              .render_data = p_context.render_data,
              .render_database = p_context.render_database,
          },
          "compute pipeline"),
      context(p_context) {
}

ComputePipelineCompilerVK::CompileResult ComputePipelineCompilerVK::compile(
    std::string_view p_resource_key, RefRes<RUShader> p_shader_resource) {
    if (p_shader_resource == nullptr || p_shader_resource->resource_key == nullptr) {
        throw std::runtime_error("Failed to compile compute pipeline: shader resource key was not propagated.");
    }
    Ref<ROShaderVK> compute_shader = compile_shader(p_shader_resource);
    ShaderMetadata shader_metadata = ShaderMetadataLoader().load(p_shader_resource);
    // Compute shaders are compiled from shader resources directly, so the shader must reflect exactly one compute entry point.
    std::string compute_entry(ShaderMetadataQuery().resolve_entry_point(shader_metadata, "compute", {}).name);
    ShaderDescriptorHeapLayoutVK descriptor_layout = collect_shader_descriptor_heap_layout(shader_metadata, "compute", compute_entry);
    Ref<ROComputePipelineVK> compute_pipeline = compile_compute_pipeline(p_resource_key, compute_shader, compute_entry);
    compute_pipeline->set_descriptor_layout(descriptor_layout);
    return CompileResult{.compute_pipeline = compute_pipeline};
}

Ref<ROComputePipelineVK> ComputePipelineCompilerVK::compile_compute_pipeline(
    std::string_view p_resource_key, Ref<ROShaderVK> p_compute_shader, std::string_view p_compute_entry) {
    ROComputePipelineVK::Builder pipeline_builder;
    pipeline_builder.set_shader_stage(p_compute_shader, p_compute_entry);
    Ref<ROComputePipelineVK> compute_pipeline = context.render_data->create_compute_pipeline(pipeline_builder);
    context.render_database->register_compiled_render_resource(p_resource_key, compute_pipeline);
    return compute_pipeline;
}

} // namespace WhiteBirdEngine

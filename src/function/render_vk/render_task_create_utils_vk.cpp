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
#include "function/render_vk/render_task_create_utils_vk.hh"
#include "core/allocator/i_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_database.hh"
#include "function/render_vk/pipeline_descriptor_bindings_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "generated/type_uuid.gen.hh"
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

namespace {

template <typename TPipeline>
Ref<TPipeline> acquire_task_pipeline(
    const RenderContextVK* p_render_data, std::string_view p_task_name, std::string_view p_pipeline_key, std::string_view p_kind) {
    if (p_render_data == nullptr || p_render_data->allocator == nullptr) {
        throw std::runtime_error(std::format("{}: render data is unavailable.", p_task_name));
    }
    if (p_render_data->render_database == nullptr) {
        throw std::runtime_error(std::format("{}: the render database is unavailable.", p_task_name));
    }
    Ref<TPipeline> pipeline =
        p_render_data->render_database->get_compiled_render_resource(p_pipeline_key, TypeUUID<TPipeline>::UUID).template dynamic_cast_ref<TPipeline>();
    if (pipeline == MEM_NULL) {
        throw std::runtime_error(std::format("{}: no {} pipeline is compiled under '{}'.", p_task_name, p_kind, p_pipeline_key));
    }
    return pipeline;
}

} // namespace

Ref<ROComputePipelineVK> acquire_task_compute_pipeline_vk(
    const RenderContextVK* p_render_data, std::string_view p_task_name, std::string_view p_pipeline_key) {
    return acquire_task_pipeline<ROComputePipelineVK>(p_render_data, p_task_name, p_pipeline_key, "compute");
}

Ref<ROGraphicsPipelineVK> acquire_task_graphics_pipeline_vk(
    const RenderContextVK* p_render_data, std::string_view p_task_name, std::string_view p_pipeline_key) {
    return acquire_task_pipeline<ROGraphicsPipelineVK>(p_render_data, p_task_name, p_pipeline_key, "graphics");
}

std::vector<TaskPushConstantVK> resolve_task_push_constants_vk(
    const DataValue& p_push_constants, std::span<const ShaderDescriptorHeapLayoutVK* const> p_layouts, std::string_view p_task_name) {
    std::vector<TaskPushConstantVK> result;
    if (p_push_constants.is_nil() || (p_push_constants.get_kind() == DataValueKind::LIST && p_push_constants.as_list().empty())) {
        return result;
    }
    if (p_push_constants.get_kind() != DataValueKind::MAP) {
        throw std::runtime_error(std::format("{}: push_constants must be a map from identifier to number.", p_task_name));
    }
    for (const auto& [name, value] : p_push_constants.as_map()) {
        const ShaderDefaultPushFieldVK* declared = nullptr;
        for (const ShaderDescriptorHeapLayoutVK* layout : p_layouts) {
            if (layout == nullptr || declared != nullptr) {
                continue;
            }
            for (const ShaderDefaultPushFieldVK& field : layout->default_fields) {
                if (field.value == ShaderDefaultPushValueVK::CUSTOM && field.name == name) {
                    declared = &field;
                    break;
                }
            }
        }
        if (declared == nullptr) {
            throw std::runtime_error(std::format("{}: no pipeline of the task declares the push constant '{}'.", p_task_name, name));
        }
        switch (declared->type) {
        case ShaderPushConstantTypeVK::UINT32:
            result.push_back({.name = name, .value = PushConstantValueVK::from_uint32(value.get<uint32_t>())});
            break;
        case ShaderPushConstantTypeVK::INT32:
            result.push_back({.name = name, .value = PushConstantValueVK::from_int32(value.get<int32_t>())});
            break;
        case ShaderPushConstantTypeVK::FLOAT32:
            result.push_back({.name = name, .value = PushConstantValueVK::from_float32(value.get<float>())});
            break;
        }
    }
    return result;
}

} // namespace WhiteBirdEngine

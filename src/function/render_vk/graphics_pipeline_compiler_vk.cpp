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
#include "function/render_vk/graphics_pipeline_compiler_vk.hh"
#include "function/render/material_texture_contract_resolver.hh"
#include "function/render/mesh_node_contract_resolver.hh"
#include "function/render/vertex_input_contract_resolver.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/shader_metadata_loader.hh"
#include "function/render/shader_metadata_query.hh"
#include "function/render_vk/pipeline_compiler_vk.hh"
#include "function/render_vk/ro_graphics_pipeline_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/ro_shader_vk.hh"
#include "function/render_vk/shader_metadata_vk_utils.hh"
#include "generated/type_uuid.gen.hh"
#include "ktxvulkan.h"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {
namespace {

std::string get_stage_entry(RefRes<char> p_stage_entry) {
    if (p_stage_entry == nullptr || p_stage_entry.get()[0] == '\0') {
        return {};
    }
    return std::string(p_stage_entry.get());
}

std::string resolve_pipeline_stage_entry(const ShaderMetadata& p_metadata, std::string_view p_stage, RefRes<char> p_stage_entry) {
    // An empty entry resolves to the stage's only reflected entry point; a stage with several must be named by the pipeline.
    return std::string(ShaderMetadataQuery().resolve_entry_point(p_metadata, p_stage, get_stage_entry(p_stage_entry)).name);
}

} // namespace

GraphicsPipelineCompilerVK::GraphicsPipelineCompilerVK(const GraphicsPipelineCompilerVKContext& p_context)
    : PipelineCompilerVK(
          PipelineCompilerVKContext{
              .allocator = p_context.allocator,
              .render_data = p_context.render_data,
              .render_database = p_context.render_database,
          },
          "graphics pipeline"),
      context(p_context) {
    if (context.allocator == nullptr || context.render_data == nullptr || context.render_database == MEM_NULL ||
        context.renderer == nullptr) {
        throw std::runtime_error("Failed to create graphics pipeline compiler: invalid context.");
    }
}

GraphicsPipelineCompilerVK::CompileResult GraphicsPipelineCompilerVK::compile(
    std::string_view p_resource_key, RefRes<RUGraphicsPipeline> p_pipeline_resource) {
    if (p_pipeline_resource == nullptr || p_pipeline_resource->vertex_stage == nullptr || p_pipeline_resource->fragment_stage == nullptr) {
        throw std::runtime_error(
            std::format("Failed to compile graphics pipeline '{}': vertex and fragment stages are required.", p_resource_key));
    }
    Ref<ROShaderVK> vertex_shader = compile_shader(p_pipeline_resource->vertex_stage);
    Ref<ROShaderVK> geometry_shader = MEM_NULL;
    Ref<ROShaderVK> fragment_shader = compile_shader(p_pipeline_resource->fragment_stage);
    ShaderMetadata vertex_metadata = ShaderMetadataLoader().load(p_pipeline_resource->vertex_stage);
    std::optional<ShaderMetadata> geometry_metadata;
    ShaderMetadata fragment_metadata = ShaderMetadataLoader().load(p_pipeline_resource->fragment_stage);
    std::string vertex_entry = resolve_pipeline_stage_entry(vertex_metadata, "vertex", p_pipeline_resource->vertex_stage_entry);
    std::string geometry_entry;
    if (p_pipeline_resource->geometry_stage != nullptr) {
        VkPhysicalDeviceFeatures supported_features{};
        vkGetPhysicalDeviceFeatures(context.render_data->physical_device, &supported_features);
        if (supported_features.geometryShader != VK_TRUE) {
            throw std::runtime_error(std::format(
                "Failed to compile graphics pipeline '{}': geometry shaders are not supported by this device.", p_resource_key));
        }
        geometry_shader = compile_shader(p_pipeline_resource->geometry_stage);
        geometry_metadata = ShaderMetadataLoader().load(p_pipeline_resource->geometry_stage);
        geometry_entry = resolve_pipeline_stage_entry(*geometry_metadata, "geometry", p_pipeline_resource->geometry_stage_entry);
    }
    std::string fragment_entry = resolve_pipeline_stage_entry(fragment_metadata, "fragment", p_pipeline_resource->fragment_stage_entry);
    ShaderDescriptorHeapLayoutVK descriptor_layout = collect_graphics_pipeline_descriptor_heap_layout(
        vertex_metadata, vertex_entry, geometry_metadata.has_value() ? &*geometry_metadata : nullptr, geometry_entry, fragment_metadata, fragment_entry);
    Ref<ROGraphicsPipelineVK> graphics_pipeline = compile_graphics_pipeline(
        p_resource_key, *p_pipeline_resource, vertex_shader, geometry_shader, fragment_shader, vertex_entry, geometry_entry, fragment_entry);

    graphics_pipeline->set_descriptor_layout(descriptor_layout);
    graphics_pipeline->vertex_input_requirement = VertexInputContractResolver().resolve_requirement(vertex_metadata, vertex_entry);
    graphics_pipeline->material_texture_contract = MaterialTextureContractResolver().resolve(vertex_metadata,
        geometry_metadata.has_value() ? &*geometry_metadata : nullptr,
        fragment_metadata,
        context.material_textures_variable_name,
        context.material_params_variable_name);
    if (ShaderMetadataQuery().find_parameter(vertex_metadata, MESH_NODE_CONTRACT_PARAMETER_NAME) != nullptr) {
        graphics_pipeline->mesh_node_contract = MeshNodeContractResolver().resolve(vertex_metadata, MESH_NODE_CONTRACT_PARAMETER_NAME);
    }
    return CompileResult{.graphics_pipeline = graphics_pipeline};
}

Ref<ROGraphicsPipelineVK> GraphicsPipelineCompilerVK::compile_graphics_pipeline(std::string_view p_resource_key,
    const RUGraphicsPipeline& p_pipeline_resource,
    Ref<ROShaderVK> p_vertex_shader,
    Ref<ROShaderVK> p_geometry_shader,
    Ref<ROShaderVK> p_fragment_shader,
    std::string_view p_vertex_stage_entry,
    std::string_view p_geometry_stage_entry,
    std::string_view p_fragment_stage_entry) {
    ROGraphicsPipelineVK::Config::ShaderConfig shader_config;
    shader_config.set_shaders(p_vertex_shader, p_fragment_shader, p_vertex_stage_entry, p_fragment_stage_entry);
    if (p_geometry_shader != MEM_NULL) {
        shader_config.set_geometry_shader(p_geometry_shader, p_geometry_stage_entry);
    }
    auto pipeline_builder = ROGraphicsPipelineVK::Builder().default_render_opaque_configuration(context.render_data, shader_config);
    pipeline_builder.config.attachment_config.set_color_formats(context.color_attachment_formats)
        .set_depth_format(context.depth_attachment_format)
        .set_stencil_format(context.stencil_attachment_format);
    for (size_t attachment_index = 0; attachment_index < context.color_attachment_formats.size(); ++attachment_index) {
        pipeline_builder.config.color_blend.add_default_color_attachment();
    }
    pipeline_builder.config.input_assembly.set_primitive_topology(context.primitive_topology);
    pipeline_builder.config.depth_test.set_depth_test(p_pipeline_resource.depth_test ? VK_TRUE : VK_FALSE);
    pipeline_builder.config.depth_test.set_depth_write(p_pipeline_resource.depth_write ? VK_TRUE : VK_FALSE);
    pipeline_builder.config.rasterization.set_polygon_mode(context.polygon_mode)
        .set_cull_mode(p_pipeline_resource.cull_backfaces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE)
        .set_conservative_rasterization(p_pipeline_resource.conservative_rasterization);
    Ref<ROGraphicsPipelineVK> graphics_pipeline = context.render_data->create_graphics_pipeline(pipeline_builder);
    context.render_database->register_compiled_render_resource(p_resource_key, graphics_pipeline);
    return graphics_pipeline;
}

Ref<ROImageViewVK> GraphicsPipelineCompilerVK::compile_texture_view(std::string_view p_resource_key, RefRes<RUTexture> p_texture_resource) {
    std::string formatted_resource_key = std::format("{}#image_view", p_resource_key);
    auto image_view = get_registered_render_object<ROImageViewVK>(
        context.render_database, formatted_resource_key, TypeUUID<ROImageViewVK>::UUID);
    if (image_view != MEM_NULL) {
        return image_view;
    }
    Ref<ROImageVK> image = compile_texture(p_resource_key, p_texture_resource);
    const ROImageVK::ExposedFields& image_fields = image->fields();
    image_view = context.render_data->create_image_view(ROImageViewVK::Builder()
                     .set_image(image)
                     .set_view_type(image_fields.view_type)
                     .set_format(image_fields.format)
                     .set_subresource_range(VkImageSubresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .baseMipLevel = 0,
                         .levelCount = image_fields.mipmap_levels,
                         .baseArrayLayer = 0,
                         .layerCount = image_fields.layers_count}));
    context.render_database->register_compiled_render_resource(formatted_resource_key, image_view);
    return image_view;
}

Ref<ROImageVK> GraphicsPipelineCompilerVK::compile_texture(std::string_view p_resource_key, RefRes<RUTexture> p_texture_resource) {
    auto image = get_registered_render_object<ROImageVK>(context.render_database, p_resource_key, TypeUUID<ROImageVK>::UUID);
    if (image != MEM_NULL) {
        return image;
    }
    if (context.render_data->ktx_vulkan_device_info == nullptr) {
        throw std::runtime_error("Failed to compile texture: libktx Vulkan device info is unavailable.");
    }
    ktxVulkanTexture uploaded_texture = context.render_data->ktx_vulkan_device_info->upload(p_texture_resource->texture.get());
    image = make_ref<ROImageVK>(context.render_data->allocator, context.render_data, uploaded_texture);
    context.render_database->register_compiled_render_resource(p_resource_key, image);
    return image;
}

} // namespace WhiteBirdEngine

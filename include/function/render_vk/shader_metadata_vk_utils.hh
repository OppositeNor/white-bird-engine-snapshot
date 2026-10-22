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
#ifndef WBE_FILE_SHADER_METADATA_VK_UTILS_HH
#define WBE_FILE_SHADER_METADATA_VK_UTILS_HH

#include "core/reflection/reflection_defs.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render/shader_metadata.hh"
#include "function/render/vertex_input_requirement.hh"
#include "function/render_vk/vertex_input_description_vk.hh"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

/**
 * @brief Resource category selected by a reflected WBE_DESCRIPTOR annotation.
 *
 * Reflected; the aliases are the type names written in WBE_DESCRIPTOR.
 */
enum class WBE_META(WBE_REFLECT_ENUM) ShaderDescriptorHeapTypeVK : uint8_t {
    UNIFORM_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(uniform_buffer)),
    STORAGE_BUFFER WBE_META(WBE_SERIALIZATION_ALIASES(storage_buffer)),
    SAMPLED_IMAGE WBE_META(WBE_SERIALIZATION_ALIASES(sampled_image)),
    STORAGE_IMAGE WBE_META(WBE_SERIALIZATION_ALIASES(storage_image)),
    SAMPLER WBE_META(WBE_SERIALIZATION_ALIASES(sampler)),
    ACCELERATION_STRUCTURE WBE_META(WBE_SERIALIZATION_ALIASES(acceleration_structure)),
};

/**
 * @brief Shader access to a descriptor-heap resource, selected by the optional third WBE_DESCRIPTOR argument.
 *
 * Uniform buffers, sampled images, samplers, and acceleration structures are always read. Storage buffers and storage images
 * default to READ_WRITE when the argument is omitted. Reflected; the aliases are the access names written in WBE_DESCRIPTOR.
 */
enum class WBE_META(WBE_REFLECT_ENUM) ShaderDescriptorAccessVK : uint8_t {
    /** The shader only reads the resource. */
    READ WBE_META(WBE_SERIALIZATION_ALIASES(read)),
    /** The shader only writes the resource. */
    WRITE WBE_META(WBE_SERIALIZATION_ALIASES(write)),
    /** The shader reads and writes the resource. */
    READ_WRITE WBE_META(WBE_SERIALIZATION_ALIASES(read_write)),
};

/**
 * @class ShaderDescriptorHeapFieldVK
 * @brief One descriptor-heap index stored in reflected pipeline push data.
 */
struct ShaderDescriptorHeapFieldVK {
    /** Path relative to the push-data parameter. */
    std::string field_path;
    /** Descriptor heap category required by this field. */
    ShaderDescriptorHeapTypeVK type = ShaderDescriptorHeapTypeVK::UNIFORM_BUFFER;
    /** Logical render-graph resource identifier resolved by the owning task. */
    std::string resource_id;
    /** Byte offset of the explicit uint2 inside push data. */
    uint32_t offset = 0;
    /** How the shader accesses the resource. */
    ShaderDescriptorAccessVK access = ShaderDescriptorAccessVK::READ;
};

/**
 * @brief Value source selected by a reflected WBE_PUSH_CONSTANT annotation.
 */
enum class ShaderDefaultPushValueVK : uint8_t {
    /** Elapsed renderer time as a float. */
    TIME,
    /** Current frame-in-flight slot as a uint32. */
    FRAME_SLOT,
    /** Task-supplied 32-bit value identified by ShaderDefaultPushFieldVK::name. */
    CUSTOM,
};

/**
 * @brief Scalar type of a reflected four-byte WBE_PUSH_CONSTANT field.
 */
enum class ShaderPushConstantTypeVK : uint8_t {
    UINT32,
    INT32,
    FLOAT32,
};

/**
 * @class ShaderDefaultPushFieldVK
 * @brief One renderer-provided or task-supplied scalar stored in reflected pipeline push data.
 */
struct ShaderDefaultPushFieldVK {
    /** Path relative to the push-data parameter. */
    std::string field_path;
    /** Source of the value copied into this field for every recorded pipeline. */
    ShaderDefaultPushValueVK value = ShaderDefaultPushValueVK::TIME;
    /** Custom push-constant identifier; empty unless value is CUSTOM. */
    std::string name;
    /** Reflected scalar type of the field. */
    ShaderPushConstantTypeVK type = ShaderPushConstantTypeVK::FLOAT32;
    /** Byte offset of the four-byte scalar inside push data. */
    uint32_t offset = 0;
};

/**
 * @class ShaderDescriptorHeapLayoutVK
 * @brief Reflected descriptor-index and push-constant contract for one pipeline.
 */
struct ShaderDescriptorHeapLayoutVK {
    /** Total byte size required by the reflected push-data block. */
    uint32_t size = 0;
    /** Shader stages which consume this push-data contract. */
    VkShaderStageFlags stage_flags = 0;
    /** Descriptor-index fields in declaration order. */
    std::vector<ShaderDescriptorHeapFieldVK> fields;
    /** Renderer-provided and custom scalar fields in declaration order. */
    std::vector<ShaderDefaultPushFieldVK> default_fields;
};

/**
 * @brief Convert a reflected shader stage string into a Vulkan stage flag.
 */
VkShaderStageFlagBits shader_stage_flag_from_metadata_stage(std::string_view p_stage);

/**
 * @brief Reflect and match one vertex entry point against the vertex bindings of a submesh.
 *
 * Every field marked WBE_VERTEX_INPUT must match an exact resource role and format declared by exactly one binding. Extra
 * resource attributes and bindings are allowed. Locations come from the reflected varying-input binding indices. Only the
 * bindings the entry reads become Vulkan bindings, numbered from zero in binding order; source_bindings maps them back to
 * p_vertex_bindings. A vertex entry without annotated fields produces an empty vertex-less state.
 *
 * @throws std::runtime_error When attributes, roles, locations, formats, arguments, or resource layout metadata are invalid,
 * or a role is declared by more than one binding.
 */
VertexInputDescriptionVK build_vertex_input_description_vk(
    const ShaderMetadata& p_metadata, std::string_view p_entry_name, std::span<const RUVertexView> p_vertex_bindings);

/**
 * @brief Match a vertex input requirement, such as ROGraphicsPipelineVK::get_vertex_input_requirement(), against the vertex
 * bindings of a submesh.
 *
 * Produces the same description as the overload taking the shader metadata the requirement was resolved from.
 *
 * @throws std::runtime_error When the resource layout metadata is invalid, or a required role is missing, has a different
 * format, or is declared by more than one binding.
 */
VertexInputDescriptionVK build_vertex_input_description_vk(
    const VertexInputRequirement& p_requirement, std::span<const RUVertexView> p_vertex_bindings);

/**
 * @brief Collect the explicit uint2 descriptor-heap push-data contract used by one shader entry point.
 *
 * A descriptor-free entry point produces an empty layout. Otherwise it may use exactly one pushConstantBuffer parameter.
 * WBE_DESCRIPTOR fields must be eight-byte uint2 values with supported descriptor-type and resource-id string arguments, and an
 * optional access argument: `read`, `write`, or `read_write`. Only storage buffers and storage images may be written.
 * A resource id ending in `[]` is per element: inside element `i` of the nearest enclosing descriptor struct array,
 * `shadowmap[]` resolves to `shadowmap[i]`, so each element can be aliased to its own graph resource. Other resource ids
 * are shared by every element. An indexed resource id outside a descriptor struct array, or one without a name, is rejected.
 * WBE_PUSH_CONSTANT fields must be four-byte scalars with one non-empty identifier. `time` (float) and `frame_slot` (uint) are
 * renderer-provided; every other identifier declares a custom uint, int, or float value supplied by the owning task.
 *
 * @throws std::runtime_error When the reflected push-data or annotation contract is malformed.
 */
ShaderDescriptorHeapLayoutVK collect_shader_descriptor_heap_layout(
    const ShaderMetadata& p_metadata, std::string_view p_stage, std::string_view p_entry_name);

/**
 * @brief Collect and merge explicit descriptor-heap push-data contracts from all supported graphics stages.
 *
 * Equal field paths must agree on descriptor category, resource id, and byte offset; their accesses are combined. Equal
 * push-constant field paths must agree on value source, custom identifier, scalar type, and byte offset. Distinct
 * fields must not overlap in push data.
 *
 * @throws std::runtime_error When stage contracts conflict.
 */
ShaderDescriptorHeapLayoutVK collect_graphics_pipeline_descriptor_heap_layout(const ShaderMetadata& p_vertex_shader_metadata,
    std::string_view p_vertex_stage_entry,
    const ShaderMetadata* p_geometry_shader_metadata,
    std::string_view p_geometry_stage_entry,
    const ShaderMetadata& p_fragment_shader_metadata,
    std::string_view p_fragment_stage_entry);

} // namespace WhiteBirdEngine

#endif

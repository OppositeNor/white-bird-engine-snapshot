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
#ifndef WBE_FILE_RENDER_RESOURCE_UNIT_HH
#define WBE_FILE_RENDER_RESOURCE_UNIT_HH

#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_int4.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "glm/ext/vector_uint4.hpp"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class RUSchemaShader
 * @brief The scheme of RUShader.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaShader : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaShader)
    // The path to the shader file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
    // The path to the shader's metadata file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string metadata;
};

/**
 * @class RUSchemaGraphicsPipeline
 * @brief The scheme of RUGraphicsPipeline.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaGraphicsPipeline : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaGraphicsPipeline)
    // The resource ID of the vertex shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> vertex_stage_id;
    // Entry function for the vertex stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> vertex_stage_entry;
    // The vertex shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> vertex_stage;
    // The resource ID of the tessellation shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> tessellation_stage_id;
    // Entry function for the tessellation stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> tessellation_stage_entry;
    // The tessellation shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> tessellation_stage;
    // The resource ID of the geometry shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> geometry_stage_id;
    // Entry function for the geometry stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> geometry_stage_entry;
    // The geometry shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> geometry_stage;
    // The resource ID of the fragment shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> fragment_stage_id;
    // Entry function for the fragment stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> fragment_stage_entry;
    // The fragment shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> fragment_stage;
    // Should enable depth test.
    WBE_META(WBE_REFLECT)
    bool depth_test = true;
    // Should enable depth write.
    WBE_META(WBE_REFLECT)
    bool depth_write = true;
    // Should cull back-facing triangles.
    WBE_META(WBE_REFLECT)
    bool cull_backfaces = true;
    /**
     * @brief Whether the pipeline overestimates primitive coverage using conservative rasterization.
     */
    WBE_META(WBE_REFLECT)
    bool conservative_rasterization = false;
};

/**
 * @class RUSchemaRayTracingPipeline
 * @brief The scheme of RURayTracingPipeline.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaRayTracingPipeline : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaRayTracingPipeline)
    // The resource ID of the ray-generation shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> raygen_stage_id;
    // Entry function for the ray-generation stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> raygen_stage_entry;
    // The ray-generation shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> raygen_stage;
    // The resource ID of the miss shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> miss_stage_id;
    // Entry function for the miss stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> miss_stage_entry;
    // The miss shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> miss_stage;
    // The resource ID of the closest-hit shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> closest_hit_stage_id;
    // Entry function for the closest-hit stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> closest_hit_stage_entry;
    // The closest-hit shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> closest_hit_stage;
    // The resource ID of the any-hit shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> any_hit_stage_id;
    // Entry function for the any-hit stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> any_hit_stage_entry;
    // The any-hit shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> any_hit_stage;
    // The resource ID of the intersection shader.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> intersection_shader_stage_id;
    // Entry function for the intersection-shader stage.
    WBE_META(WBE_REFLECT)
    std::optional<std::string> intersection_shader_stage_entry;
    // The intersection shader of the pipeline.
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaShader> intersection_shader_stage;
};

/**
 * @class RUSchemaMaterialTextureSlot
 * @brief One material texture role declared inline or by resource id.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaMaterialTextureSlot {
    /** Non-empty shader-defined texture role. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string texture_role;
    /** Inline KTX 2 texture schema. Exactly one of texture and texture_id must be present. */
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaTexture> texture;
    /** Resource ID of an already populated texture. Exactly one of texture and texture_id must be present. */
    WBE_META(WBE_REFLECT)
    std::optional<std::string> texture_id;
};

/**
 * @class RUSchemaMaterial
 * @brief The scheme of RUMaterial.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaMaterial : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaMaterial)
    /** Shader-defined texture slots used by the material. Texture roles must not be empty or duplicated. */
    WBE_META(WBE_REFLECT)
    std::vector<RUSchemaMaterialTextureSlot> textures;
    /**
     * Pipeline variant that draws the material. Draw tasks map variants to compiled pipelines; absent means
     * MATERIAL_PIPELINE_VARIANT_DEFAULT. Must not be empty or contain NUL.
     */
    WBE_META(WBE_REFLECT)
    std::optional<std::string> pipeline_variant;
};

/**
 * @class RUSchemaBinaryView
 * @brief Byte range view into a binary resource.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaBinaryView {
    /** Resource id of the binary backing this view. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string binary_id;
    /** Byte offset from the beginning of the binary. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    size_t start = 0;
    /** Byte length of this view. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    size_t size = 0;
};

/**
 * @class RUSchemaSkeleton
 * @brief The scheme of RUSkeleton.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaSkeleton : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaSkeleton)
    /**
     * Non-empty byte range of packed joints forming one tree, laid out as described by RUSkeleton::JOINT_BYTE_SIZE. The
     * first joint is the only root.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaBinaryView joints;
};

/**
 * @class RUSchemaVertexAttribute
 * @brief One vertex attribute inside a binary-backed interleaved vertex view.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaVertexAttribute {
    /** Exact shader-defined vertex role. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string role;
    /** Exact 32-bit scalar or vector attribute type: a RUVertexAttributeFormat name or alias, such as "vec3". */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string type;
    /** Byte offset inside one interleaved vertex. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    size_t offset = 0;
};

/**
 * @class RUSchemaVertexView
 * @brief Binary-backed interleaved vertex view.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaVertexView {
    /** Backing binary byte range. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaBinaryView binary;
    /** Byte stride between vertices. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    size_t stride = 0;
    /** Ordered vertex attributes. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<RUSchemaVertexAttribute> attributes;
};

/**
 * @class RUSchemaIndexView
 * @brief Binary-backed uint32 index view.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaIndexView {
    /** Backing binary byte range. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaBinaryView binary;
};

/**
 * @class RUSchemaAABB3D
 * @brief The scheme of RUAABB3D.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaAABB3D {
    /** Box center as exactly three floats `[x, y, z]`. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<float> center{0.0F, 0.0F, 0.0F};
    /** Full X extent. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float width = 0.0F;
    /** Full Y extent. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float height = 0.0F;
    /** Full Z extent. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float depth = 0.0F;
};

/**
 * @class RUSchemaSubmesh3D
 * @brief The scheme of RUSubmesh3D.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaSubmesh3D {
    /** Inline material for this submesh. */
    WBE_META(WBE_REFLECT)
    std::optional<RUSchemaMaterial> material;
    /** Referenced material for this submesh. */
    WBE_META(WBE_REFLECT)
    std::optional<std::string> material_id;
    /** Axis-aligned bounds of the submesh vertex positions. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaAABB3D aabb;
    /**
     * @brief Binary-backed vertex bindings, each an interleaved view with its own binary range, stride, and attributes.
     *
     * At least one binding is required. Every binding holds the same number of vertices in the same order, and a role is
     * declared by at most one binding. Bindings declaring the `ivec4` `joint_binding_ids` and `vec4`
     * `joint_binding_weights` roles require the mesh to reference a skeleton.
     */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<RUSchemaVertexView> vertex_bindings;
    /** Binary-backed uint32 indices. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaIndexView indices;
    /** First transform entry used by this submesh. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    size_t first_instance = 0;
    /** Number of transform entries used by this submesh. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    size_t instance_count = 0;
};

/**
 * @class RUSchemaInstance3D
 * @brief The scheme of RUInstance3D.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaInstance3D {
    /** Column-major global transform. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::mat4 global_transform{1.0F};
};

/**
 * @class RUSchemaSubmeshInstance3D
 * @brief The scheme of RUSubmeshInstance3D.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) RUSchemaSubmeshInstance3D {
    /** Submeshes in declared draw order. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<RUSchemaSubmesh3D> submeshes;
    /** Instance transforms shared by the submeshes. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<RUSchemaInstance3D> instances;
};

/**
 * @class RUSchemaMesh3D
 * @brief The scheme of RUMesh3D.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaMesh3D : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaMesh3D)
    /** Opaque submeshes drawn with face culling. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaSubmeshInstance3D opaque_instances;
    /** Alpha-tested submeshes. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaSubmeshInstance3D masked_instances;
    /** Opaque submeshes drawn without face culling. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaSubmeshInstance3D double_sided_opaque_instances;
    /** Resource ID of the skeleton the submesh joint bindings index. */
    WBE_META(WBE_REFLECT)
    std::optional<std::string> skeleton_id;
    /** Resource ID of the signed distance field of the mesh's geometry. */
    WBE_META(WBE_REFLECT)
    std::optional<std::string> sdf_id;
};

/**
 * @enum RUSDFDataStructure
 * @brief Layout of the samples of a signed distance field.
 *
 * Reflected; the aliases are the names used by RUSchemaSDF::data_structure.
 */
enum class WBE_META(WBE_REFLECT_ENUM) RUSDFDataStructure : uint8_t {
    // A 3D KTX 2 texture with one VK_FORMAT_R16_SFLOAT texel per sample.
    TEXTURE_3D WBE_META(WBE_SERIALIZATION_ALIASES(texture_3d)),
};

/**
 * @class RUSchemaSDF
 * @brief The scheme of RUSDF.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaSDF : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaSDF)
    /** Layout of the samples, deserialized from a RUSDFDataStructure name or alias such as "texture_3d". */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSDFDataStructure data_structure = RUSDFDataStructure::TEXTURE_3D;
    /** Resource ID of the texture holding the samples, laid out as data_structure describes. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string texture_id;
    /** Box covered by the samples. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    RUSchemaAABB3D aabb;
};

/**
 * @class RUShader
 * @brief Shader resource unit. Contains a word count and a MemID pointing to
 * the data chunk that stores the SPIR-V words.
 *
 * Push any chunk-owned payload data before emplacing this unit so the stored
 * references remain valid. Chunk teardown now runs registered resource-unit
 * destructors before releasing the allocator storage behind the unit and its
 * payload.
 *
 */
struct RUShader : public ResourceUnit {
    // The shader SPIR-V words.
    RefRes<uint32_t> data;
    // The number of SPIR-V words.
    size_t size;
    // The path to the metadata. Ending with an NUL terminater.
    RefRes<char> metadata_path;
};

/**
 * @class RUGraphicsPipeline
 * @brief Graphics pipeline.
 *
 */
struct RUGraphicsPipeline : public ResourceUnit {
    // The vertex shader.
    RefRes<RUShader> vertex_stage;
    // Entry function for the vertex stage.
    RefRes<char> vertex_stage_entry;
    // The tessellation shader.
    RefRes<RUShader> tessellation_stage;
    // Entry function for the tessellation stage.
    RefRes<char> tessellation_stage_entry;
    // The geometry shader.
    RefRes<RUShader> geometry_stage;
    // Entry function for the geometry stage.
    RefRes<char> geometry_stage_entry;
    // The fragment shader.
    RefRes<RUShader> fragment_stage;
    // Entry function for the fragment stage.
    RefRes<char> fragment_stage_entry;
    // Should enable depth test.
    bool depth_test = true;
    // Should enable depth write.
    bool depth_write = true;
    // Should cull back-facing triangles.
    bool cull_backfaces = true;
    /**
     * @brief Whether the pipeline overestimates primitive coverage using conservative rasterization.
     */
    bool conservative_rasterization = false;
};

/**
 * @class RURayTracingPipeline
 * @brief Ray-tracing pipeline resource with one fixed slot for each supported stage.
 */
struct RURayTracingPipeline : public ResourceUnit {
    // The ray-generation shader.
    RefRes<RUShader> raygen_stage;
    // Entry function for the ray-generation stage.
    RefRes<char> raygen_stage_entry;
    // The miss shader.
    RefRes<RUShader> miss_stage;
    // Entry function for the miss stage.
    RefRes<char> miss_stage_entry;
    // The closest-hit shader.
    RefRes<RUShader> closest_hit_stage;
    // Entry function for the closest-hit stage.
    RefRes<char> closest_hit_stage_entry;
    // The any-hit shader.
    RefRes<RUShader> any_hit_stage;
    // Entry function for the any-hit stage.
    RefRes<char> any_hit_stage_entry;
    // The intersection shader.
    RefRes<RUShader> intersection_shader_stage;
    // Entry function for the intersection-shader stage.
    RefRes<char> intersection_shader_stage_entry;
};

/**
 * @class RUShaderRole
 * @brief Chunk-owned identity for an open shader-defined resource role.
 *
 * The name is retained for exact matching and diagnostics. The hash is an acceleration value and must always match the full
 * NUL-terminated name; consumers must not treat the hash alone as identity. This type is a non-owning resource-chunk view
 * and is copyable and movable with shallow chunk references.
 */
struct RUShaderRole {
    /**
     * @brief Construct an empty role for resource population.
     */
    RUShaderRole() = default;
    /**
     * @brief Destroy a non-owning role view.
     */
    ~RUShaderRole() = default;
    /**
     * @brief Copy a shallow role view.
     */
    RUShaderRole(const RUShaderRole&) = default;
    /**
     * @brief Move a shallow role view.
     */
    RUShaderRole(RUShaderRole&&) noexcept = default;
    /**
     * @brief Copy a shallow role view.
     */
    RUShaderRole& operator=(const RUShaderRole&) = default;
    /**
     * @brief Move a shallow role view.
     */
    RUShaderRole& operator=(RUShaderRole&&) noexcept = default;

    /**
     * @brief Construct a populated shallow role view; validation occurs when the role is accessed.
     */
    RUShaderRole(RefRes<char> p_name, HashCode p_hash) : name(p_name), hash(p_hash) {
    }

    /** Non-empty NUL-terminated role name. */
    RefRes<char> name;
    /** Runtime hash of name. */
    HashCode hash = 0;

    /**
     * @brief Return and validate the retained role name.
     * @throws std::runtime_error When the name is missing or empty, or its hash is inconsistent.
     */
    std::string_view get_name() const;
    /**
     * @brief Test a role using the hash and exact retained name.
     * @throws std::runtime_error When this role or the requested name is invalid.
     */
    bool matches(std::string_view p_name) const;
};

/**
 * @brief Pipeline variant of a material that does not declare one; draw tasks draw it with their default pipeline.
 */
inline constexpr std::string_view MATERIAL_PIPELINE_VARIANT_DEFAULT = "default";

/**
 * @class RUMaterial
 * @brief Typed material resource unit.
 */
struct RUMaterial : public ResourceUnit {
    /** One resolved texture-role slot. */
    struct TextureSlot {
        /** Exact shader-defined texture role. */
        RUShaderRole role;
        /** Resolved texture resource. */
        RefRes<RUTexture> texture;
    };
    /** Chunk-owned contiguous texture-slot storage. */
    RefRes<TextureSlot> textures;
    /** Number of entries in textures. */
    size_t texture_count = 0;
    /** Chunk-owned NUL-terminated pipeline variant; null for MATERIAL_PIPELINE_VARIANT_DEFAULT. */
    RefRes<char> pipeline_variant;

    /**
     * @brief Get the pipeline variant that draws the material.
     * @return The declared variant, or MATERIAL_PIPELINE_VARIANT_DEFAULT when none is declared.
     */
    std::string_view get_pipeline_variant() const {
        return pipeline_variant == nullptr ? MATERIAL_PIPELINE_VARIANT_DEFAULT : std::string_view(pipeline_variant.get());
    }
};

/**
 * @enum RUVertexAttributeFormat
 * @brief Supported tightly packed 32-bit vertex attribute formats.
 *
 * Reflected; the aliases are the shader type names used by RUSchemaVertexAttribute::type.
 */
enum class WBE_META(WBE_REFLECT_ENUM) RUVertexAttributeFormat : uint8_t {
    FLOAT WBE_META(WBE_SERIALIZATION_ALIASES(float)),
    FLOAT2 WBE_META(WBE_SERIALIZATION_ALIASES(vec2)),
    FLOAT3 WBE_META(WBE_SERIALIZATION_ALIASES(vec3)),
    FLOAT4 WBE_META(WBE_SERIALIZATION_ALIASES(vec4)),
    INT WBE_META(WBE_SERIALIZATION_ALIASES(int)),
    INT2 WBE_META(WBE_SERIALIZATION_ALIASES(ivec2)),
    INT3 WBE_META(WBE_SERIALIZATION_ALIASES(ivec3)),
    INT4 WBE_META(WBE_SERIALIZATION_ALIASES(ivec4)),
    UINT WBE_META(WBE_SERIALIZATION_ALIASES(uint)),
    UINT2 WBE_META(WBE_SERIALIZATION_ALIASES(uvec2)),
    UINT3 WBE_META(WBE_SERIALIZATION_ALIASES(uvec3)),
    UINT4 WBE_META(WBE_SERIALIZATION_ALIASES(uvec4)),
};

/**
 * @class RUBinaryView
 * @brief Non-owning byte range view into a chunk-owned binary resource.
 */
struct RUBinaryView {
    /** Resolved backing binary. */
    RefRes<RUBinary> binary;
    /** Byte offset from the beginning of the binary. */
    size_t start = 0;
    /** Byte length of this view. */
    size_t size = 0;
};

/**
 * @class RUSkeleton
 * @brief Skeleton resource unit: a joint tree stored as a parent-before-child array in a binary resource.
 *
 * Joint 0 is the only root. Every other joint's parent index is smaller than its own index, so global bind transforms can be
 * accumulated in array order. The joints are packed in a chunk-owned binary resource, which is resolved after the resource
 * chunk loads; read them with get_joint(). The unit is a shallow chunk view and is copyable and movable.
 */
struct RUSkeleton : public ResourceUnit {
    /**
     * @brief Byte size of one packed joint: a little-endian `int32` parent index followed by the 16 column-major `float32`
     * values of the bind-local transform, without padding.
     */
    static constexpr size_t JOINT_BYTE_SIZE = sizeof(int32_t) + (16 * sizeof(float));

    /**
     * @brief One joint of the skeleton.
     */
    struct Joint {
        /** Index of the parent joint, or -1 for the root joint. */
        int32_t parent_index = -1;
        /** Column-major bind-pose transform relative to the parent joint, or to the mesh space for the root joint. */
        glm::mat4 bind_local_transform{1.0F};
    };
    /** Byte range of the packed joints, exactly joint_count * JOINT_BYTE_SIZE bytes long. */
    RUBinaryView joints;
    /** Number of joints in the skeleton. */
    size_t joint_count = 0;

    /**
     * @brief Read one joint from the backing binary.
     * @param p_index Index of the joint.
     * @return The decoded joint.
     * @throws std::runtime_error If the index is out of range, the backing binary is missing or smaller than the joint
     * range, or the joint breaks the single-root, parent-before-child order.
     */
    Joint get_joint(size_t p_index) const;
};

/**
 * @class RUVertexView
 * @brief Binary-backed mutable interleaved vertex data and its fixed layout; one vertex binding of a submesh.
 *
 * The layout is immutable after loading. Vertex bytes may be changed by the mutation methods, which are not thread-safe. A
 * renderer update is required before changes reach the GPU. The resource is a non-owning view into its resource chunk and is
 * copyable and movable with shallow chunk references.
 */
struct RUVertexView {
    /**
     * @brief One attribute in declaration and packing order.
     */
    struct Attribute {
        /** Exact shader-defined vertex role. */
        RUShaderRole role;
        /** Exact 32-bit scalar or vector format. */
        RUVertexAttributeFormat format = RUVertexAttributeFormat::FLOAT;
        /** Byte offset inside one interleaved vertex. */
        size_t offset = 0;
        /** Byte size inside one interleaved vertex. */
        size_t size = 0;
    };

    /** Mutable tightly interleaved vertex bytes. */
    RUBinaryView binary;
    /** Ordered fixed-layout attributes. */
    RefRes<Attribute> attributes;
    /** Number of entries in attributes. */
    size_t attribute_count = 0;
    /** Byte stride between vertices. */
    size_t vertex_stride = 0;
    /** Number of interleaved vertices. */
    size_t vertex_count = 0;

    /**
     * @brief Swap two complete interleaved vertices.
     * @throws std::runtime_error When storage is missing or either vertex is out of bounds.
     */
    void swap_vertices(size_t p_first, size_t p_second);
    /**
     * @brief Return the byte offset of a role inside one vertex.
     * @throws std::runtime_error When the role is empty, duplicated, or absent, or layout storage is missing.
     */
    size_t get_vertex_attribute_offset(std::string_view p_role) const;
};

/**
 * @class RUIndexView
 * @brief Binary-backed uint32 index view.
 */
struct RUIndexView {
    /** Backing byte range containing uint32 indices. */
    RUBinaryView binary;
    /** Number of entries in indices. */
    size_t index_count = 0;
};

/**
 * @class RUAABB3D
 * @brief Axis-aligned bounds of a submesh's vertex positions, in the same space as its vertices.
 *
 * Bounds describe the geometry before the submesh's instance transforms are applied.
 */
struct RUAABB3D {
    /** Box center. */
    glm::vec3 center{0.0F};
    /** Full X extent. */
    float width = 0.0F;
    /** Full Y extent. */
    float height = 0.0F;
    /** Full Z extent. */
    float depth = 0.0F;
};

/**
 * @class RUSubmesh3D
 * @brief One material, geometry range, and instance range of a mesh category.
 *
 * A non-owning view into its resource chunk; copyable and movable with shallow chunk references. Vertex mutation is not
 * thread-safe and requires recompiling the mesh's render resources to reach the GPU.
 */
struct RUSubmesh3D {
    /** Referenced material. */
    RefRes<RUMaterial> material;
    /** Axis-aligned bounds of the vertex positions. */
    RUAABB3D aabb;
    /**
     * @brief Chunk-owned vertex bindings in declaration order.
     *
     * Each binding is an interleaved view with its own binary range and stride. All bindings hold vertex_count vertices in
     * the same order, and each role is declared by at most one binding. Skinned submeshes declare the `ivec4`
     * `joint_binding_ids` indexing the mesh skeleton joints and the matching `vec4` `joint_binding_weights`.
     */
    RefRes<RUVertexView> vertex_bindings;
    /** Number of entries in vertex_bindings. */
    size_t vertex_binding_count = 0;
    /** Number of vertices held by every binding. */
    size_t vertex_count = 0;
    /** Binary-backed uint32 indices. */
    RUIndexView indices;
    /** First transform entry used by this submesh. */
    size_t first_instance = 0;
    /** Number of transform entries used by this submesh. */
    size_t instance_count = 0;

    /**
     * @brief Swap two vertices in every vertex binding.
     * @throws std::runtime_error When storage is missing or either vertex is out of bounds.
     */
    void swap_vertices(size_t p_first, size_t p_second);
    /** Set an exact float scalar attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, float p_value);
    /** Set an exact float2 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::vec2& p_value);
    /** Set an exact float3 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::vec3& p_value);
    /** Set an exact float4 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::vec4& p_value);
    /** Set an exact signed 32-bit scalar attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, int32_t p_value);
    /** Set an exact signed 32-bit integer2 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::ivec2& p_value);
    /** Set an exact signed 32-bit integer3 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::ivec3& p_value);
    /** Set an exact signed 32-bit integer4 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::ivec4& p_value);
    /** Set an exact unsigned 32-bit scalar attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, uint32_t p_value);
    /** Set an exact unsigned 32-bit integer2 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::uvec2& p_value);
    /** Set an exact unsigned 32-bit integer3 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::uvec3& p_value);
    /** Set an exact unsigned 32-bit integer4 attribute. */
    void set_vertex_attribute(size_t p_vertex_index, std::string_view p_role, const glm::uvec4& p_value);

    /**
     * @brief Return the vertex bindings.
     * @throws std::runtime_error When binding storage is missing.
     */
    std::span<const RUVertexView> get_vertex_bindings() const;
    /**
     * @brief Return the vertex bindings for mutation.
     * @throws std::runtime_error When binding storage is missing.
     */
    std::span<RUVertexView> get_vertex_bindings();
    /**
     * @brief Return the index of the vertex binding declaring a role.
     * @throws std::runtime_error When the role is empty, absent, or declared by more than one binding, or storage is
     * invalid.
     */
    size_t get_vertex_binding_index(std::string_view p_role) const;
    /**
     * @brief Return the byte offset of a role inside one vertex of the binding declaring it.
     * @throws std::runtime_error Under the conditions of get_vertex_binding_index().
     */
    size_t get_vertex_attribute_offset(std::string_view p_role) const;
    /**
     * @brief Return the number of vertices.
     * @throws std::runtime_error When a binding's storage is invalid or holds a different number of vertices.
     */
    size_t get_vertices_num() const;
};

/**
 * @class RUInstance3D
 * @brief One instance transform of a mesh category.
 */
struct RUInstance3D {
    /** Column-major global transform. */
    glm::mat4 global_transform{1.0F};
};

/**
 * @class RUSubmeshInstance3D
 * @brief Submeshes of one mesh category and the instance transforms their instance ranges index.
 */
struct RUSubmeshInstance3D {
    /** Submeshes in declared draw order. */
    RefRes<RUSubmesh3D> submeshes;
    /** Number of entries in submeshes. */
    size_t submesh_count = 0;
    /** Instance transforms shared by the submeshes. */
    RefRes<RUInstance3D> instances;
    /** Number of entries in instances. */
    size_t instance_count = 0;
};

/**
 * @class RUSDF
 * @brief Signed distance field resource unit: distances sampled on a uniform grid over a box, stored in a binary resource.
 *
 * Distances are negative inside the surface. The grid divides aabb into one cell per texel along each axis, and each
 * sample lies at the center of its cell, so sampling the texture at normalized coordinates (p - aabb minimum) / aabb size
 * reads the distance at p. A mesh's field is in the mesh's space, before any instance or entity transform. The texture is
 * resolved after the resource chunk loads. The unit is a shallow chunk view and is copyable and movable.
 */
struct RUSDF : public ResourceUnit {
    /** Layout of the samples. */
    RUSDFDataStructure data_structure = RUSDFDataStructure::TEXTURE_3D;
    /** Texture holding the samples: for TEXTURE_3D, a 3D VK_FORMAT_R16_SFLOAT texture with one texel per sample. */
    RefRes<RUTexture> texture;
    /** Box covered by the samples. */
    RUAABB3D aabb;

    /**
     * @brief Get the number of samples along X, Y, and Z.
     * @throws std::runtime_error If the texture is missing or is not a 3D VK_FORMAT_R16_SFLOAT texture with loaded data.
     */
    glm::uvec3 get_extent() const;
    /**
     * @brief Read one sample from the texture.
     * @param p_coordinate Sample index along X, Y, and Z.
     * @return The signed distance at the sample.
     * @throws std::runtime_error If the coordinate is outside the extent, or under the conditions of get_extent().
     */
    float get_sample(const glm::uvec3& p_coordinate) const;
};

/**
 * @class RUMesh3D
 * @brief 3D mesh resource unit: opaque, masked, and double-sided opaque submesh categories.
 *
 * Static and dynamic meshes share this representation; the categories may share vertex and index binaries. The payload
 * lifetime belongs to the resource chunk.
 */
struct RUMesh3D : public ResourceUnit {
    /** Opaque submeshes drawn with face culling. */
    RUSubmeshInstance3D opaque_instances;
    /** Alpha-tested submeshes. */
    RUSubmeshInstance3D masked_instances;
    /** Opaque submeshes drawn without face culling. */
    RUSubmeshInstance3D double_sided_opaque_instances;
    /** Referenced skeleton, or null when the mesh is not skinned. */
    RefRes<RUSkeleton> skeleton;
    /** Referenced signed distance field of the mesh's geometry, or null when the mesh has none. */
    RefRes<RUSDF> sdf;

    /**
     * @brief Compute the axis-aligned bounds of every submesh of every category, with their instance transforms applied.
     *
     * Each submesh's bounds are transformed by every instance it draws, so the result is in the mesh's space, before any
     * entity transform.
     *
     * @return The bounds enclosing every drawn submesh instance.
     * @throws std::runtime_error When the mesh draws no submesh instance.
     */
    RUAABB3D compute_aabb() const;
};

} // namespace WhiteBirdEngine

#endif

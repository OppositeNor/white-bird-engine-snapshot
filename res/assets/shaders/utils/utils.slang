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
#ifndef WBE_FILE_UTILS_SLANG
#define WBE_FILE_UTILS_SLANG

/**
 * @brief Reports an error when the condition is false, only in shader-debug builds.
 * @details Disabled builds do not evaluate any argument or guard the following operation.
 * @param p_condition Required condition, evaluated once when shader debugging is enabled.
 * @param p_error Stable shader error code.
 * @param p_frame_slot Renderer frame slot receiving the error.
 */
#ifdef WBE_SHADER_DEBUG
#define WBE_SHADER_DEBUG_ASSERT(p_condition, p_error, p_frame_slot) \
    do { \
        if (!(p_condition)) { \
            report_shader_debug_error(p_error, p_frame_slot); \
        } \
    } while (0)
#else
#define WBE_SHADER_DEBUG_ASSERT(p_condition, p_error, p_frame_slot)
#endif

/** Stable error codes shared with the CPU shader-debug reader. */
enum class ShaderDebugError {
    DEBUG_BUFFER_FULL = 1,
    SVO_NODE_INDEX_OVERFLOW = 2,
    SVO_LEAF_INDEX_OVERFLOW = 3,
    SVO_DEFERRED_TASK_INDEX_OVERFLOW = 4,
    SVO_DEEPER_THAN_EXPECTED = 5,
    SVO_SHALLOWER_THAN_EXPECTED = 6,
    OCTREE_LEAF_LIST_OVERFLOW = 7,
    OCTREE_DEBUG_NODE_LIST_OVERFLOW = 8,
    SVO_VOXEL_DATA_INDEX_OVERFLOW = 9,
};

/** Shader-visible buffer descriptor reserved by the renderer. */
static const uint WBE_SHADER_DEBUG_DESCRIPTOR_INDEX = 0;

#ifdef WBE_SHADER_DEBUG
/**
 * @brief Atomically append one error to the renderer-owned shader-debug buffer.
 * @param p_error Error code to append.
 * @param p_frame_slot Renderer frame slot whose independent report region receives the error.
 */
void report_shader_debug_error(ShaderDebugError p_error, uint p_frame_slot) {
    RWStructuredBuffer<uint> debug_buffer = ResourceDescriptorHeap[WBE_SHADER_DEBUG_DESCRIPTOR_INDEX];
    static const uint FRAME_ENTRY_CAPACITY = 512;
    static const uint FRAME_WORD_COUNT = FRAME_ENTRY_CAPACITY + 2;
    uint frame_offset = p_frame_slot * FRAME_WORD_COUNT;
    uint capacity = debug_buffer[frame_offset + 1];
    uint entry_index;
    InterlockedAdd(debug_buffer[frame_offset], 1, entry_index);

    // Reserve the last physical entry for an unambiguous overflow marker. No normal writer can race with this store.
    if (capacity > 0 && entry_index < capacity - 1) {
        debug_buffer[frame_offset + entry_index + 2] = uint(p_error);
        return;
    }
    if (capacity > 0) {
        InterlockedMin(debug_buffer[frame_offset], capacity);
        InterlockedExchange(debug_buffer[frame_offset + capacity + 1], uint(ShaderDebugError::DEBUG_BUFFER_FULL));
    }
}

#endif

/** Marks a vertex entry-point field and selects a resource attribute by role. */
[__AttributeUsage(_AttributeTargets.Var)]
struct WBE_VERTEX_INPUTAttribute {
    /** Vertex attribute role; an empty value selects the annotated field name. */
    string vertex_role = "";
};

/** Marks a push-data uint2 as an index into one descriptor-heap arena. */
[__AttributeUsage(_AttributeTargets.Var)]
struct WBE_DESCRIPTORAttribute {
    /** Descriptor category interpreted by the Vulkan pipeline compiler. */
    string descriptor_type;
    /** Logical render-graph resource identifier resolved by the owning task. */
    string resource_id;
    /**
     * Shader access: "read", "write", or "read_write". Omitted means "read_write" for storage buffers and storage images and
     * "read" for every other descriptor type, which can only be read.
     */
    string access = "";
};

/**
 * Marks a four-byte push-data scalar. "time" (float) and "frame_slot" (uint) are supplied by the renderer; any other
 * identifier is a custom uint, int, or float value the owning render task sets with set_push_constant().
 */
[__AttributeUsage(_AttributeTargets.Var)]
struct WBE_PUSH_CONSTANTAttribute {
    /** Renderer value identifier or custom push-constant identifier. */
    string value;
};

/** Marks a struct as an engine-discoverable data type in reflection metadata. */
[__AttributeUsage(_AttributeTargets.Struct)]
struct WBE_DATA_TYPEAttribute {
    /** Optional logical type name; an empty value means to use the struct name. */
    string type_name = "";
};

/**
 * @brief The input data from the vertex buffer
 */
struct Vertex3DTexturedInput {
    /**
     * @brief The position of the vertex.
     */
    [WBE_VERTEX_INPUT]
    float3 position;
    /**
     * @brief The normal of the vertex.
     */
    [WBE_VERTEX_INPUT]
    float3 normal;
    /**
     * @brief The tanglent vector of the vertex.
     */
    [WBE_VERTEX_INPUT]
    float3 tangent;
    /**
     * @brief The bitangent vector of the vertex.
     */
    [WBE_VERTEX_INPUT]
    float3 bitangent;
    /**
     * @brief The uv value.
     */
    [WBE_VERTEX_INPUT]
    float2 uv;
};

/**
 * @brief The information of the camera.
 */
struct CameraInfo {
    /**
     * @brief The view matrix.
     */
    float4x4 view;
    /**
     * @brief The projection matrix.
     */
    float4x4 proj;
    /**
     * @brief The world-space camera position.
     */
    float4 world_position;
};

/** Maximum number of bindless material entries addressable by one draw pass. */
static const uint WBE_MAX_MATERIAL_COUNT = 32;

/** Marks an integer material parameter as the descriptor index for one texture role. */
[__AttributeUsage(_AttributeTargets.Var)]
struct WBE_TEXTUREAttribute {
    /** Texture role; an empty value selects the annotated variable name. */
    string texture_role = "";
};

#ifdef WBE_REFLECTION_METADATA
/** Reflection-only view of the material texture contract. */
[WBE_DATA_TYPE]
struct MaterialTextureContractMetadata {
    SamplerState material_sampler;
    Texture2D<float4> textures[];
};
#endif

/** Fixed descriptor indices for one material's textures. */
[WBE_DATA_TYPE]
struct MaterialParams {
    /** Albedo texture descriptor index. */
    [WBE_TEXTURE("albedo")]
    int albedo_index;
    /** Normal texture descriptor index. */
    [WBE_TEXTURE("normal")]
    int normal_index;
    /** RMA texture descriptor index. */
    [WBE_TEXTURE("rma")]
    int rma_index;
};

/** Fixed descriptor-index table for every material addressed by an indirect draw. */
[WBE_DATA_TYPE]
struct MaterialParamsBuffer {
    /** Material entries indexed by the indirect draw's material index. */
    MaterialParams materials[WBE_MAX_MATERIAL_COUNT];
};

/** Per-submesh metadata for one mesh draw entry. */
[WBE_DATA_TYPE]
struct SubmeshData {
    /** Material table entry used by this submesh. */
    uint material_id;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

/** Transform payload of one mesh node, or of the whole mesh. */
[WBE_DATA_TYPE]
struct NodeData {
    /** Engine-space column-major transform. */
    float4x4 transform;
    /** Precomputed inverse-transpose used for static normal transforms. */
    float4x4 normal_transform;
};

/** Per-drawn-instance metadata linking one submesh to one node transform. */
[WBE_DATA_TYPE]
struct InstanceData {
    /** Index into the mesh.submeshes storage buffer. */
    uint submesh_index;
    /** Index into the mesh.nodes storage buffer. */
    uint node_index;
    uint padding_0;
    uint padding_1;
};

#ifdef WBE_REFLECTION_METADATA
/** Reflection-only view of the mesh storage-buffer contract. */
[WBE_DATA_TYPE]
struct MeshContractMetadata {
    StructuredBuffer<SubmeshData> submeshes;
    StructuredBuffer<NodeData> nodes;
    StructuredBuffer<InstanceData> instances;
};
#endif

/**
 * @brief Mesh storage buffers matching the renderer's mesh binding contract.
 */
struct MeshDescriptorIndices {
    [WBE_DESCRIPTOR("storage_buffer", "mesh_submeshes", "read")]
    uint2 submeshes;
    /** Instance-local transforms, relative to the mesh transform. */
    [WBE_DESCRIPTOR("storage_buffer", "mesh_nodes", "read")]
    uint2 nodes;
    [WBE_DESCRIPTOR("storage_buffer", "mesh_instances", "read")]
    uint2 instances;
#ifdef WBE_MESH_TRANSFORM
    /** One NodeData holding the whole mesh's transform and normal transform. Only declared by shader variants that
     * define WBE_MESH_TRANSFORM, i.e. variants drawing placed (entity) meshes. */
    [WBE_DESCRIPTOR("storage_buffer", "mesh_transform", "read")]
    uint2 transform;
#endif
};

/**
 * @brief Load one instance's world transform. With WBE_MESH_TRANSFORM this is the mesh transform applied after the
 * instance-local transform; without it the instance-local transform is already the world transform.
 * @param p_mesh Mesh descriptor indices of the drawn mesh.
 * @param p_node_index Index into the mesh's node table, from InstanceData::node_index.
 */
NodeData load_mesh_node(MeshDescriptorIndices p_mesh, uint p_node_index) {
    StructuredBuffer<NodeData> nodes = ResourceDescriptorHeap[p_mesh.nodes.x];
    NodeData local_node = nodes[p_node_index];
#ifdef WBE_MESH_TRANSFORM
    StructuredBuffer<NodeData> mesh_transform = ResourceDescriptorHeap[p_mesh.transform.x];
    NodeData mesh_node = mesh_transform[0];
    NodeData result;
    result.transform = mul(mesh_node.transform, local_node.transform);
    result.normal_transform = mul(mesh_node.normal_transform, local_node.normal_transform);
    return result;
#else
    return local_node;
#endif
}

/**
 * @brief Mathematical constant pi.
 */
static const float WBE_PI = 3.14159265359;

/**
 * @brief Decode a two-channel tangent-space normal map sample and reconstruct its positive Z component.
 * @param p_encoded_normal Encoded tangent-space X and Y components in [0, 1].
 * @return The decoded tangent-space normal on the positive-Z hemisphere.
 */
float3 decode_normal_map(float2 p_encoded_normal) {
    float2 normal_xy = p_encoded_normal * 2.0 - 1.0;
    float normal_z = sqrt(saturate(1.0 - dot(normal_xy, normal_xy)));
    return float3(normal_xy, normal_z);
}

float3 transform_static_normal(float4x4 p_normal_transform, float3 p_normal) {
    return normalize(mul((float3x3)p_normal_transform, p_normal));
}

/**
 * @brief Compute the Chebyshev upper bound for a value from its first two moments.
 * @param p_moments The distribution mean and second moment.
 * @param p_value The value whose probability bound is requested.
 * @param p_minimum_variance The lower variance bound used for numerical stability.
 * @return The probability upper bound clamped to [0, 1].
 */
float chebyshev_upper_bound(float2 p_moments, float p_value, float p_minimum_variance) {
    // var(x) = E[x^2] - E[x]^2.
    float variance = max(p_moments.y - p_moments.x * p_moments.x, p_minimum_variance);
    float value_delta = p_value - p_moments.x;
    // p_max(t) = var(x) / (var(x) + (t - E[x])^2).
    return saturate(variance / (variance + value_delta * value_delta));
}

// -----------------------------------------------------------------------------
// Octahedron normal encoding.
//
// Encodes a unit vector in R^3 into a 2D square in [-1, 1]. Suitable for
// storing into an R16G16_SNORM (or higher precision) render target as a
// compact normal representation for G-buffers and similar use cases.
// -----------------------------------------------------------------------------

/**
 * @brief Helper used by ::octahedron_encode to fold the lower hemisphere onto the unit square.
 * @param p_v The xy components of a normalized direction projected onto the octahedron.
 * @return The folded xy coordinates clamped to the [-1, 1] square.
 */
float2 octahedron_wrap(float2 p_v) {
    return (1.0 - abs(p_v.yx)) * (float2(p_v.x >= 0.0 ? 1.0 : -1.0, p_v.y >= 0.0 ? 1.0 : -1.0));
}

/**
 * @brief Encode a unit-length direction vector into a 2-component value in [-1, 1] using octahedron mapping.
 * @param p_n The unit-length direction to encode.
 * @return The encoded 2D coordinate, suitable for storage in an R16G16_SNORM (or higher precision) target.
 */
float2 octahedron_encode(float3 p_n) {
    float3 n = p_n / max(abs(p_n.x) + abs(p_n.y) + abs(p_n.z), 0.00001);
    float2 r = n.z >= 0.0 ? n.xy : octahedron_wrap(n.xy);
    return clamp(r, -1.0, 1.0);
}

/**
 * @brief Decode an octahedron-mapped 2D coordinate back into a unit-length direction vector.
 * @param p_f The encoded 2D coordinate, as produced by ::octahedron_encode.
 * @return The decoded unit-length direction.
 */
float3 octahedron_decode(float2 p_f) {
    float3 n = float3(p_f.x, p_f.y, 1.0 - abs(p_f.x) - abs(p_f.y));
    float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

// -----------------------------------------------------------------------------
// Color space helpers.
// -----------------------------------------------------------------------------

/**
 * @brief Convert a linear-space color to sRGB using the approximate gamma 2.2 curve.
 * @param p_color The linear-space color (non-negative components).
 * @return The sRGB-encoded color.
 */
float3 linear_to_srgb(float3 p_color) {
    return pow(max(p_color, float3(0.0)), float3(1.0 / 2.2));
}

/**
 * @brief Convert an sRGB-encoded color to linear space using the approximate gamma 2.2 curve.
 * @param p_color The sRGB-encoded color (non-negative components).
 * @return The linear-space color.
 */
float3 srgb_to_linear(float3 p_color) {
    return pow(max(p_color, float3(0.0)), float3(2.2));
}

/**
 * @brief Packs a non-negative float3 into unsigned 11/11/10-bit floats.
 * @param p_rgb The value to pack; negative components clamp to zero.
 * @return The packed value, with x in the lowest 11 bits and z in the highest 10 bits.
 */
uint pack_r11g11b10(float3 p_rgb) {
    p_rgb = max(p_rgb, 0.0);
    // Prevent becomming infinity after rounding.
    p_rgb = min(p_rgb, asfloat(0x477C0000));

    uint r = ((f32tof16(p_rgb.x) + 8)  >> 4)  & 0x000007FF;
    uint g = ((f32tof16(p_rgb.y) + 8)  << 7)  & 0x003FF800;
    uint b = ((f32tof16(p_rgb.z) + 16) << 17) & 0xFFC00000;

    return r | g | b;
}

/**
 * @brief Unpacks a value produced by pack_r11g11b10().
 * @param p_value The packed value.
 * @return The unpacked float3.
 */
float3 unpack_r11g11b10(uint p_value) {
    float r = f16tof32((p_value << 4)  & 0x7FF0);
    float g = f16tof32((p_value >> 7)  & 0x7FF0);
    float b = f16tof32((p_value >> 17) & 0x7FE0);

    return float3(r, g, b);
}

uint2 z_order_swizzle_8x8(uint2 p_group_id, uint2 p_group_thread_id) {
    uint linear_index = p_group_thread_id.y * 8 + p_group_thread_id.x;
    uint swizzled_x = ((linear_index >> 0) & 0x1) | ((linear_index >> 1) & 0x2) | ((linear_index >> 2) & 0x4);
    uint swizzled_y = ((linear_index >> 1) & 0x1) | ((linear_index >> 2) & 0x2) | ((linear_index >> 3) & 0x4);
    return p_group_id * 8 + uint2(swizzled_x, swizzled_y);
}

/**
 * @brief Spreads the low 10 bits of a value so that two zero bits separate each pair of consecutive bits.
 * @param p_value The value to spread. Bits above bit 9 are discarded.
 * @return The spread value, with input bit i at output bit 3 * i.
 */
uint morton_spread_bits_3d(uint p_value) {
    uint result = p_value & 0x000003FF;
    result = (result | (result << 16)) & 0x030000FF;
    result = (result | (result << 8)) & 0x0300F00F;
    result = (result | (result << 4)) & 0x030C30C3;
    result = (result | (result << 2)) & 0x09249249;
    return result;
}

/**
 * @brief Swizzles a 3D coordinate into a 30-bit Morton (Z-order) code.
 * @param p_coordinate The coordinate to encode. Each component must be less than 1024; higher bits are discarded.
 * @return The Morton code, interleaved as ...z1 y1 x1 z0 y0 x0 with x in the least significant bit.
 */
uint morton_encode_3d(uint3 p_coordinate) {
    return morton_spread_bits_3d(p_coordinate.x)
        | (morton_spread_bits_3d(p_coordinate.y) << 1)
        | (morton_spread_bits_3d(p_coordinate.z) << 2);
}

/**
 * @brief Flattens a 3D coordinate into a linear index, with x varying fastest and z slowest.
 * @param p_coordinate The coordinate to flatten; x must be less than p_width and y less than p_height.
 * @param p_width The grid's extent along x.
 * @param p_height The grid's extent along y.
 * @return p_coordinate.x + p_width * (p_coordinate.y + p_height * p_coordinate.z).
 */
uint flatten_index_3d(uint3 p_coordinate, uint p_width, uint p_height) {
    return p_coordinate.x + p_width * (p_coordinate.y + p_height * p_coordinate.z);
}

/**
 * @brief Recovers the 3D coordinate of a linear index, inverting flatten_index_3d().
 * @param p_index The linear index.
 * @param p_width The grid's extent along x; must be nonzero.
 * @param p_height The grid's extent along y; must be nonzero.
 * @return The coordinate whose flattened index is p_index.
 */
uint3 unflatten_index_3d(uint p_index, uint p_width, uint p_height) {
    return uint3(p_index % p_width, (p_index / p_width) % p_height, p_index / (p_width * p_height));
}

/**
 * @brief Recovers the 3D coordinate of a linear index like unflatten_index_3d(), checking it against the grid's depth.
 * @details In shader-debug builds, an index past the grid's cell count reports p_overflow_error; the result is not clamped
 * otherwise.
 * @param p_index The linear index.
 * @param p_extent The grid's extent along each axis; x and y must be nonzero.
 * @param p_overflow_error Error reported when p_index lies past the grid.
 * @param p_frame_slot Renderer frame slot receiving the error.
 * @return The coordinate whose flattened index is p_index.
 */
uint3 unflatten_index_3d_checked(uint p_index, uint3 p_extent, ShaderDebugError p_overflow_error, uint p_frame_slot) {
    uint3 coordinate = unflatten_index_3d(p_index, p_extent.x, p_extent.y);
    WBE_SHADER_DEBUG_ASSERT(coordinate.z < p_extent.z, p_overflow_error, p_frame_slot);
    return coordinate;
}

uint morton_delta(uint p_code1, uint p_code2) {
    return firstbithigh(p_code1 ^ p_code2);
}

float random(float2 p_seed) {
    return frac(sin(dot(p_seed, float2(12.9898, 78.233))) * 43758.5453123);
}

#endif

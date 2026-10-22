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
#ifndef WBE_FILE_MESH_RESOURCES_VK_HH
#define WBE_FILE_MESH_RESOURCES_VK_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/i_bindable_render_resource_vk.hh"
#include "function/render_vk/mesh_draw_batch_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_sampler_vk.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/material_texture_contract.hh"
#include "function/render/mesh_node_contract.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include <utility>
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

/** Shader resource id of the submesh table declared by MeshDescriptorIndices. */
inline constexpr std::string_view REND_RES_MESH_SUBMESHES = "mesh_submeshes";
/** Shader resource id of the instance-local node-transform table declared by MeshDescriptorIndices. */
inline constexpr std::string_view REND_RES_MESH_NODES = "mesh_nodes";
/** Shader resource id of the instance-link table declared by MeshDescriptorIndices. */
inline constexpr std::string_view REND_RES_MESH_INSTANCES = "mesh_instances";
/** Shader resource id of the whole-mesh transform declared by MeshDescriptorIndices; it holds one GeometryNodeGPU. */
inline constexpr std::string_view REND_RES_MESH_TRANSFORM = "mesh_transform";
/** Shader resource id of the reflected material parameter table. */
inline constexpr std::string_view REND_RES_MESH_MATERIAL_PARAMS = "material_params";
/** Shader resource id of the material sampler. */
inline constexpr std::string_view REND_RES_MESH_MATERIAL_SAMPLER = "material_sampler";

/**
 * @enum MeshPartVK
 * @brief A category of a mesh's submeshes, drawn by its own indexed-indirect commands.
 *
 * Reflected; the lowercase aliases are the names used in render graph recipes.
 */
enum class WBE_META(WBE_REFLECT_ENUM, WBE_REFLECT_SCRIPT) MeshPartVK : uint8_t {
    /** Opaque submeshes drawn with face culling. */
    OPAQUE WBE_META(WBE_SERIALIZATION_ALIASES(opaque)),
    /** Alpha-tested submeshes. */
    MASKED WBE_META(WBE_SERIALIZATION_ALIASES(masked)),
    /** Opaque submeshes drawn without face culling. */
    DOUBLE_SIDED WBE_META(WBE_SERIALIZATION_ALIASES(double_sided)),
};

/**
 * @class MeshResourcesVK
 * @brief Pipeline-independent Vulkan resources compiled from a mesh's categories, with the source mesh they came from.
 *
 * Shaders place each instance at a whole-mesh transform × its immutable instance-local transform. The whole-mesh transform
 * is not part of these resources: every draw binds REND_RES_MESH_TRANSFORM to a buffer owned by whoever places the mesh, so
 * one compiled mesh can be drawn at several places.
 *
 * The resources are independent of any shader. Draw batches record the mesh's vertex layout, which a pipeline's vertex input
 * requirement is matched against when it draws them (MeshDrawBatchVK::resolve_vertex_input()). Materials are kept as the
 * ordered material rows and a sampled view of every material texture; the material parameter table a pipeline reads depends
 * on its material texture contract, so acquire_material_table() builds one table per distinct contract on first request.
 *
 * MeshCompilerVK registers compiled meshes in the RenderDatabase under the mesh resource key. The object owns no pipeline or
 * render-task state. It is neither copyable nor movable and is shared through Ref. It and every contained render object are
 * rendering-thread-confined. bind() imports each present member under `<key>.<member>`, using the member names below, with
 * every material table built so far; absent optional categories are not declared.
 */
class WBE_META(WBE_RENDER_OBJECT) MeshResourcesVK final : public IRenderObjectVK, public IBindableRenderResourceVK {
public:
    /** Member name prefix of the vertex buffers; vertex buffer i is the member `vertices.<i>`. */
    static constexpr std::string_view VERTICES = "vertices";
    /** Member name of the shared uint32 index buffer. */
    static constexpr std::string_view INDICES = "indices";
    /** Member name of the opaque indirect commands. */
    static constexpr std::string_view OPAQUE_DRAW_BATCHES = "opaque_draw_batches";
    /** Member name of the opaque double-sided indirect commands. */
    static constexpr std::string_view OPAQUE_DOUBLE_SIDED_DRAW_BATCHES = "opaque_double_sided_draw_batches";
    /** Member name of the masked indirect commands. */
    static constexpr std::string_view MASKED_DRAW_BATCHES = "masked_draw_batches";
    /** Member name of the submesh table. */
    static constexpr std::string_view SUBMESHES = "submeshes";
    /** Member name of the instance-local node-transform table. */
    static constexpr std::string_view NODES = "nodes";
    /** Member name of the instance-link table. */
    static constexpr std::string_view INSTANCES = "instances";
    /** Member name prefix of the material parameter tables; table i is the member `material_params.<i>`. */
    static constexpr std::string_view MATERIAL_PARAMS = "material_params";
    /** Member name of the material sampler. */
    static constexpr std::string_view MATERIAL_SAMPLER = "material_sampler";
    /** Member name of the material texture list. */
    static constexpr std::string_view TEXTURES = "textures";

    MeshResourcesVK() = delete;
    virtual ~MeshResourcesVK() override = default;
    MeshResourcesVK(const MeshResourcesVK&) = delete;
    MeshResourcesVK(MeshResourcesVK&&) noexcept = delete;
    MeshResourcesVK& operator=(const MeshResourcesVK&) = delete;
    MeshResourcesVK& operator=(MeshResourcesVK&&) noexcept = delete;

    /**
     * @brief Construct empty mesh resource storage owned by the render data's allocator.
     * @throws std::runtime_error If p_render_data or its allocator is null.
     */
    explicit MeshResourcesVK(const RenderContextVK* p_render_data);

    virtual TypeID get_type_uuid() const override;

    /**
     * @brief Register the members only these resources own as `<p_name>.<member>`, using the member names below.
     *
     * Vertex and index buffers are shared through the RenderDatabase and keep the keys of their binaries. Material tables
     * built later by acquire_material_table() are registered too.
     */
    virtual void apply_debug_name(std::string_view p_name) override;

    /**
     * @brief Return the member name of one material parameter table, `material_params.<p_index>`.
     */
    static std::string get_material_params_member(size_t p_index);

    /**
     * @brief Return the member name of a part's indexed-indirect commands.
     */
    static std::string_view get_draw_commands_member(MeshPartVK p_part);

    /**
     * @brief Check that a pipeline's mesh node layout matches the node table these resources upload.
     * @throws std::runtime_error If the layouts differ.
     */
    static void validate_node_contract(const MeshNodeContract& p_contract);

    /**
     * @brief Return the member name of one vertex buffer, `vertices.<p_index>`.
     */
    static std::string get_vertex_buffer_member(size_t p_index);
    /** @brief Return the number of vertex buffers, one per vertex binary the mesh's vertex bindings read. */
    size_t get_vertex_buffer_count() const {
        return vertex_buffers.size();
    }
    /**
     * @brief Acquire one vertex buffer.
     * @throws std::runtime_error If p_index is out of bounds.
     */
    Ref<ROBufferVK> acquire_vertex_buffer(size_t p_index) const;
    /**
     * @brief Return the index of the vertex buffer compiled from a vertex binary.
     * @throws std::runtime_error If no vertex buffer was compiled from p_binary.
     */
    size_t get_vertex_buffer_index(RefRes<RUBinary> p_binary) const;

    /** @brief Acquire the shared uint32 index buffer. */
    Ref<ROBufferVK> acquire_indices() const {
        return indices;
    }

    /** @brief Acquire opaque indexed-indirect draw commands. */
    Ref<ROBufferVK> acquire_opaque_draw_batches() const {
        return opaque_draw_batches;
    }

    /**
     * @brief Acquire opaque double-sided indexed-indirect draw commands.
     */
    Ref<ROBufferVK> acquire_opaque_double_sided_draw_batches() const {
        return opaque_double_sided_draw_batches;
    }

    /** @brief Acquire masked indexed-indirect draw commands. */
    Ref<ROBufferVK> acquire_masked_draw_batches() const {
        return masked_draw_batches;
    }

    /** @brief Acquire a part's indexed-indirect draw commands; null when the part has no draws. */
    Ref<ROBufferVK> acquire_draw_commands(MeshPartVK p_part) const;

    /** @brief Return the number of draw batches of a part. */
    size_t get_draw_batch_count(MeshPartVK p_part) const;

    /**
     * @brief Get one draw batch of a part.
     * @throws std::runtime_error If p_index is out of bounds.
     */
    const MeshDrawBatchVK& get_draw_batch(MeshPartVK p_part, size_t p_index) const;

    /** @brief Acquire per-submesh information addressed by instance records. */
    Ref<ROBufferVK> acquire_submeshes() const {
        return submeshes;
    }

    /** @brief Acquire per-node transforms addressed by instance records. */
    Ref<ROBufferVK> acquire_nodes() const {
        return nodes;
    }

    /** @brief Acquire drawn-instance links between submeshes and nodes. */
    Ref<ROBufferVK> acquire_instances() const {
        return instances;
    }

    /** @brief Acquire the reflected material parameter table. */
    /**
     * @brief Get the material parameter table built for a material texture contract, building it on first request.
     *
     * Tables are cached per distinct contract (MaterialTextureContract::matches()), so pipelines sharing a contract share one
     * table. Every material row is filled from the mesh's materials, and texture roles a material lacks from the default
     * material; texture indices are descriptor-heap indices of the views compiled with the mesh. Call on the rendering thread.
     *
     * @return The table index, for acquire_material_params() and get_material_params_member().
     * @throws std::runtime_error If the mesh has more materials than the contract's table holds, the default material is missing
     * or lacks a role the contract declares, or the descriptor limit is exceeded.
     */
    size_t acquire_material_table(const MaterialTextureContract& p_contract);

    /** @brief Return the number of material parameter tables built so far. */
    size_t get_material_table_count() const {
        return material_tables.size();
    }

    /**
     * @brief Acquire a material parameter table built by acquire_material_table().
     * @throws std::runtime_error If p_index is out of bounds.
     */
    Ref<ROBufferVK> acquire_material_params(size_t p_index) const;

    /** @brief Acquire the sampler used by the mesh materials. */
    Ref<ROSamplerVK> acquire_material_sampler() const {
        return material_sampler;
    }

    /** @brief Acquire every sampled image retained by the material table. */
    Ref<RenderResourceListVK> acquire_textures() const {
        return textures;
    }

    /** @brief Return the source mesh the resources were compiled from. */
    RefRes<RUMesh3D> get_mesh() const {
        return mesh;
    }

    /** @brief Return the number of opaque draw batches. */
    size_t get_opaque_draw_batch_count() const {
        return opaque_batches.size();
    }

    /** @brief Return one opaque draw batch. */
    const MeshDrawBatchVK& get_opaque_draw_batch(size_t p_index) const;

    /**
     * @brief Return the number of opaque double-sided draw batches.
     */
    size_t get_opaque_double_sided_draw_batch_count() const {
        return opaque_double_sided_batches.size();
    }

    /**
     * @brief Return one opaque double-sided draw batch.
     */
    const MeshDrawBatchVK& get_opaque_double_sided_draw_batch(size_t p_index) const;

    /** @brief Return the number of masked draw batches. */
    size_t get_masked_draw_batch_count() const {
        return masked_batches.size();
    }

    /** @brief Return one masked draw batch. */
    const MeshDrawBatchVK& get_masked_draw_batch(size_t p_index) const;

    /**
     * @brief Import every present member under `<p_resource_id>.<member>`.
     * @throws std::runtime_error If p_resource_id is empty or a required member is missing.
     */
    virtual void bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const override;

private:
    friend class MeshCompilerVK;

    struct MaterialTable {
        MaterialTextureContract contract;
        Ref<ROBufferVK> buffer = MEM_NULL;
    };

    RenderAllocator* allocator = nullptr;
    RefRes<RUMesh3D> mesh = MEM_NULL;
    Vector<Ref<ROBufferVK>, RenderAllocator> vertex_buffers;
    Vector<RefRes<RUBinary>, RenderAllocator> vertex_binaries;
    Ref<ROBufferVK> indices = MEM_NULL;
    Ref<ROBufferVK> opaque_draw_batches = MEM_NULL;
    Ref<ROBufferVK> opaque_double_sided_draw_batches = MEM_NULL;
    Ref<ROBufferVK> masked_draw_batches = MEM_NULL;
    Ref<ROBufferVK> submeshes = MEM_NULL;
    Ref<ROBufferVK> nodes = MEM_NULL;
    Ref<ROBufferVK> instances = MEM_NULL;
    // Material rows in submesh-table order, the material filling missing roles, and the view compiled for each texture.
    std::vector<RefRes<RUMaterial>> materials;
    RefRes<RUMaterial> default_material = MEM_NULL;
    std::vector<std::pair<RefRes<RUTexture>, Ref<ROImageViewVK>>> texture_views;
    std::vector<MaterialTable> material_tables;
    Ref<ROSamplerVK> material_sampler = MEM_NULL;
    Ref<RenderResourceListVK> textures = MEM_NULL;
    Vector<MeshDrawBatchVK, RenderAllocator> opaque_batches;
    Vector<MeshDrawBatchVK, RenderAllocator> opaque_double_sided_batches;
    Vector<MeshDrawBatchVK, RenderAllocator> masked_batches;
};

} // namespace WhiteBirdEngine

#endif

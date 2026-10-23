# Vulkan Renderer

This page describes the Vulkan backend (`function/render_vk`): the frame, device requirements, memory, descriptor heaps,
vertex input, meshes, and acceleration structures. Render graphs and their Lua/Teal recipes are described in
[render_graph.md](render_graph.md).

## Resource creation

`RendererVK::get_render_context()` returns the renderer-owned `RenderContextVK`. The context keeps a stable address across
swap-chain recreation and exposes synchronous resource creation on the rendering thread:

```cpp
const RenderContextVK* context = renderer->get_render_context();
BufferDescVK description;
description.set_usage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).initial_data(bytes);
Ref<ROBufferVK> buffer = context->create_buffer(description);
```

The context creates buffers, images, image views, samplers, staging buffers, shaders, and graphics/compute pipelines.
Buffer and image creation also accept their alias descriptions for binding existing device memory. Include the relevant
`ro_*.hh` header for the description and returned resource type. Existing names such as `ROBufferVK::Builder` remain aliases
for the standalone descriptions, and `builder.create_ref(context)` delegates to the same context factory.

Factories return owning `Ref` handles and use the renderer's allocator. The context, allocator, and Vulkan device must
outlive resources and their deferred destruction. Resource creation from another thread belongs inside an `add_job()`
callback. Default image views remain available through `image->get_image_view_ref()`.

## Frame

`RendererVK::tick()` runs on the rendering thread and executes the active graph:

1. Recreate the swap chain if the window was resized.
2. Advance the frame-in-flight slot and call each plugin's `on_frame_begin()`.
3. Run jobs queued with `IRenderer::add_job()`, then delayed calls registered with `register_delay_call()`.
4. Check the shader-debug buffer (shader-debug builds).
5. Submit GPU jobs (see below).
6. If a graph is active: acquire a swap-chain image, set the swap-chain view container's current slot, and execute the graph.
   Its tasks are recorded and submitted in topological order, each into its own command buffer.
7. Capture a frame if `IRenderer::request_frame_capture()` asked for one, present, and wait on the in-flight fence.

The last graphics submission signals a per-swap-chain-image binary semaphore, which `present()` waits on before
`vkQueuePresentKHR`.

### Threading

Render objects and renderer-backend helpers are confined to the rendering thread and have no internal synchronization. Code
on another thread submits work through `IRenderer::add_job()` (or `try_add_job()`); do not add locks to render objects.

`register_gpu_job(GPUJobVK)` (`GPUJobVK` is `std::function<void(VkCommandBuffer)>`) queues one-off GPU work. Jobs are
batched into one graphics command buffer submitted in the same frame before the graph runs, followed by a full memory barrier
and a timeline-semaphore signal that both compute queues wait on. The returned future resolves after submission and holds a
completion fence.

`register_delay_call(call, frames)` runs a call after the given number of frames; render objects use it to defer destruction
until no frame in flight can still use them.

### Plugins

`RenderPluginVK` (`render_plugin_vk.hh`) extends the renderer. Hooks: `try_enable()`, required instance and device
extensions, `on_before_instance_creation()` / `on_after_instance_created()`, `on_before_device_creation()` /
`on_after_device_created()`, `update_render_context()`, `configure_descriptor_heaps()` / `create_descriptor_arenas()`,
`on_frame_begin()`, `on_render_graph_registered()`, `on_before_task_recorded()` / `on_after_task_recorded()`, and
`on_renderer_terminating()`. Device features are requested with the protected `enable_device_feature()` helper, which merges
an existing `pNext` structure instead of appending a duplicate.

Built-in plugins, enabled through `RendererCapabilities` (`i_renderer.hh`):

- `RayTracingPluginVK` (`ray_tracing`, `ray_query`): acceleration structures, ray-tracing pipelines, and the
  acceleration-structure descriptor arena.
- `TimestampQueryPluginVK` (`timestamp_query`): per-task GPU timings, written with the graph structure to
  `gpu_timestamp_report.json`. `python export_render_graph.py` turns the report into a Mermaid or Graphviz graph.
- `DescriptorSetPluginVK` (`descriptor_set`): a classic descriptor pool for third-party pipelines such as ImGui.

Other capabilities are `shader_debug` and `conservative_rasterization`.

## Device Requirements

Device extensions: swap chain, `VK_EXT_vertex_input_dynamic_state`, `VK_EXT_descriptor_heap`,
`VK_KHR_shader_untyped_pointers`, `VK_KHR_maintenance5`, and `VK_KHR_unified_image_layouts`, plus those plugins require.
Required features include buffer device address, shader demote-to-helper, shader untyped pointers, descriptor heaps,
vertex-input dynamic state, multi-draw indirect, and a memory type that is both host-visible and device-local for heap
storage. Timeline semaphores, synchronization2, dynamic rendering, and the Vulkan memory model are enabled. There is no
fallback for devices that lack them.

## Device Memory

`ROBufferVK` and `ROImageVK` bind to a `DeviceMemoryVK`, which owns one VMA allocation. Their ordinary `Builder` creates the
resource, allocates a `Ref<DeviceMemoryVK>` for its requirements, and binds it. An `AliasBuilder` instead takes an existing
`Ref<DeviceMemoryVK>` and an offset (`set_memory()`) and creates the resource through `DeviceMemoryVK::create_buffer()` or
`create_image()`, which check the memory type, alignment, and remaining size before creating anything, then use
`vmaCreateAliasingBuffer2()` / `vmaCreateAliasingImage2()`. Memory for several aliases can come from explicit requirements
(`DeviceMemoryVK::Builder::set_requirements()`) or from an existing resource (`acquire_memory()`). Dedicated allocations cannot
be aliased.

Every bound resource holds a reference to its memory until its own delayed destruction. Aliased resources share contents, so
their use must be synchronized. Swap-chain and libktx images do not use `DeviceMemoryVK`. The render graph does not alias
resources.

## Descriptor Heaps

`DescriptorHeapAllocatorVK` is a generic heap and arena allocator. `RendererVK` creates one resource heap with buffer
(index 0 reserved for the shader-debug buffer), sampled-image, and storage-image arenas, and a separate sampler heap. Plugins
add arenas; the ray-tracing plugin adds an acceleration-structure arena to the resource heap whose stride is the
device-reported acceleration-structure descriptor size. Buffer and image regions use their descriptor type's device-reported
stride; unified stride is not enabled.

`ROBufferVK`, `ROImageViewVK`, `ROSamplerVK`, and `ROAccelerationStructureVK` own their descriptor indices, allocated lazily
by their getters (or transferred through builders), and defer release for the frames in flight.

Shaders receive each resource as an aligned `uint2` field in a single push-data block, marked
`WBE_DESCRIPTOR("TYPE", "resource_id", "ACCESS")`:

- `TYPE` is `uniform_buffer`, `storage_buffer`, `sampled_image`, `storage_image`, `sampler`, or `acceleration_structure`.
- `resource_id` names the graph resource, directly or through the task's `RendTaskDescVK::alias()`. An id ending in `[]`
  resolves per element of the enclosing descriptor struct array (`shadowmap[0]`, `shadowmap[1]`, ...).
- `ACCESS` is optional: `read`, `write`, or `read_write`. Only storage buffers and images may be written; they default to
  `read_write`, so annotate read-only ones with `read`.

`WBE_PUSH_CONSTANT` marks four-byte scalars in the same block: `"time"` (`float`) and `"frame_slot"` (`uint`) are filled by
the renderer, and any other name is a custom value set on compute tasks with `set_push_constant()` or the recipe params'
`push_constants` table.

The render graph binds both heaps before each task records. Pipeline-run tasks own a `PipelineDescriptorBindingsVK` that binds
every reflected field to its graph resource at build, checks the resource type, reports the reflected accesses to the graph,
and pushes the indices and push constants when recording. A missing resource or incompatible type fails graph construction.

## Materials

Material texture semantics are shader metadata, not renderer enums. Integer index fields in the global `MaterialParams`
struct declare texture roles with `WBE_TEXTURE`: an explicit string names the role, and an omitted one uses the field name.
`rma` is the canonical role for the roughness, metallic, and ambient-occlusion channel pack. Materials carry no
`material_type`; the graphics pipeline defines the shader model.

The contract names are fixed in `utils/utils.slang`: the reflection-only `MaterialTextureContractMetadata.material_sampler` and
`.textures` describe the resource shape, and `MaterialParamsBuffer.materials` is the runtime table. Applications may configure
only the `material_textures` and `material_params` base variable names (`GraphicsPipelineCompilerVKContext`) and the default
material key (`MeshCompilerVK::Config::default_material_resource_key`).

`GraphicsPipelineCompilerVK` resolves each pipeline's `MaterialTextureContract` with `MaterialTextureContractResolver`. A
compiled mesh builds one material table per distinct contract on demand (`MeshResourcesVK::acquire_material_table()`, through
`MeshMaterialResourceBuilderVK`): each texture becomes its global sampled-image heap index, written at the reflected field
offset, with missing roles taken from the default material. The renderer never interprets roles.

Texture and vertex annotations share one backend-neutral role resolver: exact names are authoritative, hashes only
accelerate matching, and duplicate names and distinct-name hash collisions are rejected.

## Vertex Input

Mesh geometry lives in `RUBinary` sidecar files. A submesh (`RUSubmesh3D`) references one or more `RUVertexView` vertex
bindings and one `RUIndexView`. Every binding holds the same vertex count in the same order, is tightly interleaved with its
own stride, and lists attributes with an open string role, byte offset, and reflected format (`float`, `vec2`, `vec3`, `vec4`,
`int`, `ivec2`–`ivec4`, `uint`, `uvec2`–`uvec4`). Each role is declared by at most one binding. Index views contain
contiguous `uint32` indices. Attribute values may be changed through `RUSubmesh3D`, which is not thread-safe; recompile the
affected mesh resources and acceleration structures afterwards.

Vertex shader fields opt in with `WBE_VERTEX_INPUT`; an omitted argument uses the field name:

```slang
struct VertexInput
{
    [WBE_VERTEX_INPUT("position")]
    float3 object_position;

    [WBE_VERTEX_INPUT]
    float2 uv;
};
```

Each annotated role must be provided by exactly one binding with the same format; unconsumed attributes are allowed, and
locations come from the shader's reflection. A pipeline exposes a mesh-independent `VertexInputRequirement`. Resolving it
against a submesh's bindings yields a `VertexInputDescriptionVK` containing only the bindings the shader reads, renumbered
from 0, with `source_bindings` mapping back. The backend supplies it through `VK_EXT_vertex_input_dynamic_state` before each
draw batch; there is no static vertex-input path. Direct draws use `RTRunGraphicsPipelineVK::Builder::set_indexed_draw()` with
one vertex-buffer source per binding, or `set_draw_vertex_count()` for vertex-less draws.

## Meshes

### Resources

`RUMesh3D` (manifest type `mesh`) holds three `RUSubmeshInstance3D` categories: `opaque_instances`, `masked_instances`, and
`double_sided_opaque_instances`. Each stores its submeshes and `RUInstance3D` transforms (`global_transform`, 16 column-major
floats). A submesh references a material, an AABB, its vertex bindings, an index view, and a
`[first_instance, instance_count)` range. A mesh may reference an `RUSkeleton` (`skeleton_id`); skinned submeshes then declare
the `joint_binding_ids` (`ivec4`) and `joint_binding_weights` (`vec4`) roles. Skinning itself is done by the application's
shaders (see `vulkan_draw_skinned_mesh_test.cpp` and `skeleton/skeleton_utils.slang`).

### Compilation

`MeshCompilerVK(renderer).compile(mesh, config)` compiles every category of one `RUMesh3D` into one pipeline-independent
`MeshResourcesVK` and registers it in the `RenderDatabase` under the mesh's resource key; `acquire(key)` returns the
registered mesh or loads and compiles it. `Config` sets the default material key and extra vertex and index buffer usage.

1. `MeshBufferCompilerVK` validates storage, strides, and ranges, uploads one vertex buffer per distinct vertex binary, and
   one index buffer (all drawable submeshes must share one index binary). Uploads are cached in the `RenderDatabase` under the
   binary's identity plus usage flags, so identical bytes are uploaded once.
2. The submesh, node, and instance tables (`GeometrySubmeshGPU`, `GeometryNodeGPU`, `GeometryInstanceGPU`) are assembled for
   all categories, in the order opaque, double-sided, masked, and uploaded as storage buffers.
3. `MeshDrawBatchPlannerVK` groups submeshes into draw batches independently of any pipeline: draws batch together when their
   bindings share a layout, binary, `start % stride` residue, and relative offsets, and the vertex span fits `int32_t`. Batches
   split at `maxDrawIndirectCount`. Each submesh keeps one `VkDrawIndexedIndirectCommand` in a per-category indirect buffer with
   indirect and storage usage, so a compute task can rewrite it when ordered by edges.
4. The material sampler is created; material tables are built later per pipeline contract.

Vertex and index bytes keep their original binary layouts; side-band tables tell the shader which submesh and transform each
indirect draw uses.

### Graph members

Importing a mesh with `add_resource(key, *mesh)` registers its members under `<key>.<member>`: `vertices.<i>`, `indices`,
`opaque_draw_batches`, `opaque_double_sided_draw_batches` and `masked_draw_batches` (for present categories), `submeshes`,
`nodes`, `instances`, `material_params.<i>` (for each material table built), `material_sampler`, and `textures` (a
`RenderResourceListVK` whose entries register as `textures[<index>]`).

Mesh shaders declare the tables in `MeshDescriptorIndices` (`utils/utils.slang`) under shared ids: `mesh_submeshes`,
`mesh_nodes`, `mesh_instances`, `material_params`, `material_sampler`, and, in shader variants compiled with
`WBE_MESH_TRANSFORM`, `mesh_transform` (`REND_RES_MESH_*` in C++). `load_mesh_node()` returns the world transform; its layout
is validated against the reflection-only `ParameterBlock<MeshContractMetadata> mesh`.

### Drawing

Draw meshes with `RTGraphicsDrawMeshVK` (recipe type `graphics_draw_mesh`, or `RTGraphicsDrawMeshVK::add_to_graph()` in C++).
Its params (`RTParamsGraphicsDrawMeshVK`) name the mesh resource key, the graphics pipeline's render-database key, the
`parts` to draw (`opaque`, `masked`, `double_sided`), an optional `transform` resource, the render extent, clear values, and
load ops. When the task is added it acquires the compiled mesh, imports it under its key if not yet imported (so shared
meshes are compiled and imported once), builds and imports the material table for the pipeline's contract, and aliases the
`REND_RES_MESH_*` ids the pipeline declares. The caller aliases the render targets. Do not assemble mesh draws from
`RTRunGraphicsPipelineIndirectVK` by hand; use that task directly only for non-mesh indexed indirect draws.

Static meshes are drawn where their instance transforms put them, with shaders that do not declare `mesh_transform`. Placed
(dynamic) meshes use `WBE_MESH_TRANSFORM` variants: `params.transform` names a one-`GeometryNodeGPU` buffer owned by whatever
places the mesh, so one compiled mesh can be drawn at several places and moving it costs one matrix. The render tests'
`EntityDrawData` (`tests/executable/function/render/entity_draw_data.hh`) owns one such buffer per frame in flight and imports
it as `<entity key>.transform`.

```lua
local params: wbe.RTParamsGraphicsDrawMeshVK = {
    mesh = "model_cube_static.mesh",
    pipeline = "draw_cube_unshaded_pipeline",
    parts = { "opaque" },
}
-- task: { id = "run_static_mesh", type = "graphics_draw_mesh", params = params,
--         aliases = { ["render_target_colors[0]"] = "swap_chain" } }
```

### Pipelines

`GraphicsPipelineCompilerVK(context).compile(key, pipeline_resource)` compiles an `RUGraphicsPipeline` for the attachment
formats in its context and registers it in the `RenderDatabase` under the key, which recipe params reference.
`ComputePipelineCompilerVK(context).compile(key, shader)` does the same for compute shaders. Compile pipelines once, outside the
graph's rebuild callback, and keep them alive. Ray-tracing pipelines are built with `ROPipelineRayTracingVK::Builder`.

## Render Targets

Graphics tasks render into the task-local resources `render_target_colors[<index>]` and `render_target_depth`
(`RenderTargetsVK::color_id()` and `RenderTargetsVK::DEPTH_ID`). The number of color targets and the use of depth follow the
pipeline's attachment formats; every pipeline in one task shares that layout. Map them to graph resources with `alias()` (or
recipe `aliases`).

- Clear values follow target order: all colors, then depth. Empty clears every target to zero.
- Color and depth load ops control how a follow-up task reuses targets produced earlier; a loaded target is also read.
- Task ids, aliases, and dependencies belong to the task descriptor, not the task configuration.

## Acceleration Structures

Ray-tracing acceleration structures are explicit resources; the renderer keeps no published scene geometry.

1. Compile the mesh with `MeshAccelerationStructureVK::GEOMETRY_BUFFER_USAGE` added to the `MeshCompilerVK::Config` vertex and
   index buffer usage.
2. Create `MeshAccelerationStructureCompilerVK` from the base render data and
   `RayTracingPluginVK::get_base_ray_tracing_data()`, then `compile(mesh, mesh_transform)`. The result holds one BLAS per
   drawable opaque or opaque double-sided submesh, one TLAS instance per mesh instance, and the build inputs. Moving the mesh
   requires compiling again.
3. Import it with `add_resource(key, *acceleration_structure)`, which registers the TLAS as `<key>.top_level`. Record its build
   with an `RTBuildAccelerationStructuresVK` task writing `<key>.top_level`, wrapped in `RTTriggeredVK` to run once, and order
   consumers after it with edges.

The deferred test supplies the build task to its recipe as an imported task and adds the acceleration-structure import to
the descriptor the recipe builder returns (`tests/executable/function/render/deferred_test/deferred_render_graph.cpp`).

## Render Database

`RenderDatabase` (`function/render/render_database.hh`) is a weak registry of compiled render objects keyed by name and type:
compiled pipelines, meshes, cached binary uploads, and graph-owned resources of keyed graphs (`<graph key>.<resource id>`,
with `[slot]` for per-frame copies). Registering an object also names it for debugging. It holds entries weakly, so the
owner must keep each object alive.

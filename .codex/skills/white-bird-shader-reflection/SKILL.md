---
name: white-bird-shader-reflection
description: Use and extend White Bird Engine's Slang shader-reflection pipeline. Apply when loading reflected shader metadata from RUShader resources, querying parameters, consuming user attributes or layout data, writing WBE_DESCRIPTOR / WBE_PUSH_CONSTANT / WBE_VERTEX_INPUT / WBE_TEXTURE contracts, wiring mesh or material shader resources, diagnosing reflection contracts, or adding descriptor-heap type support (buffers, images, samplers, acceleration structures).
---

# White Bird Shader Reflection

Use the backend-neutral reflection model (`function/render`) for inspection and contracts, and keep Vulkan translation in
`function/render_vk`. Keep resource acquisition, metadata loading, and querying separate.

The Vulkan path is descriptor-heap based: shaders receive heap indices in a single push-data block and access the
renderer-owned global heaps. There are no descriptor sets, binding numbers, or bindless spaces.

## Acquire metadata from a shader resource

```cpp
auto shader = ResourceManager::get_singleton()->get_resource<RUShader>("shader_resource_key");
ShaderMetadata metadata = ShaderMetadataLoader().load(shader);

const ShaderMetadataParameter* parameter = ShaderMetadataQuery().find_parameter(metadata, "camera_info");
if (parameter == nullptr) {
    throw std::runtime_error("Required shader parameter 'camera_info' is missing.");
}
```

Include `resource/resource_manager/resource_manager.hh`, `function/render/shader_metadata_loader.hh`, and
`function/render/shader_metadata_query.hh`. `RUShader` holds only the SPIR-V words and its `metadata_path`; stage entry names
live on `RUGraphicsPipeline` / `RURayTracingPipeline`.

Pointers returned by `ShaderMetadataQuery` view the `ShaderMetadata`: keep it alive and unmodified. `find_parameter` and
`find_field` return `nullptr` for absence and throw for empty or duplicate names; `find_entry_point` throws when missing or
duplicated; `resolve_entry_point` allows an omitted name only when the stage has exactly one entry.

## Choose the consumer

- Name-based inspection: `ShaderMetadataQuery` (one field level at a time; there is no dotted-path walker).
- Annotation roles: `ShaderRoleFieldResolver`; do not hand-roll attribute parsing or role hashing.
- Vertex inputs: `VertexInputContractResolver` with `WBE_VERTEX_INPUT`.
- Material textures: `MaterialTextureContractResolver` with `WBE_TEXTURE`.
- Mesh node layout: `MeshNodeContractResolver` (`NodeData` stride and transform offsets from the global `mesh` parameter).
- Descriptor-heap push-data layout: `collect_shader_descriptor_heap_layout()` /
  `collect_graphics_pipeline_descriptor_heap_layout()` in `function/render_vk/shader_metadata_vk_utils.hh`.
- Binding at record time: the task-owned `PipelineDescriptorBindingsVK`; it is driven by pipeline-run tasks, not called
  by hand.

## Write shader contracts

Descriptor fields are aligned `uint2` values in the entry point's single push-data block:

```slang
struct PushData {
    [WBE_DESCRIPTOR("uniform_buffer", "camera_info")]
    uint2 camera_info;
    [WBE_DESCRIPTOR("storage_buffer", "lights", "read")]
    uint2 lights;
    [WBE_PUSH_CONSTANT("time")]
    float time;
};
```

- `WBE_DESCRIPTOR("TYPE", "resource_id", "ACCESS")`: `TYPE` is `uniform_buffer`, `storage_buffer`, `sampled_image`,
  `storage_image`, `sampler`, or `acceleration_structure`; the resource id is required; `ACCESS` (`read`, `write`,
  `read_write`) is optional and storage resources default to `read_write`, so annotate read-only ones with `read`. The
  resource id is the graph resource name (or a task-local name mapped with `RendTaskDescVK::alias()`), never a heap index.
- A resource id ending in `[]` (`shadowmap[]`) resolves per element of the nearest enclosing descriptor struct array
  (`shadowmap[0]`, `shadowmap[1]`, ...), so each element can be aliased separately. Other ids are shared by all elements.
- `WBE_PUSH_CONSTANT("time")` (`float`) and `"frame_slot"` (`uint`) are filled by the renderer. Any other name is a custom
  `uint`, `int`, or `float` set with `set_push_constant(name, PushConstantValueVK::from_uint32(...))` on compute tasks, or
  through the `push_constants` table of their recipe params. Recording crashes if a custom constant was never set.
- Declarations needed only for metadata contracts go under `#ifdef WBE_REFLECTION_METADATA`.
- Do not introduce a closed texture-role enum or branch on concrete roles such as `albedo`, `normal`, or `rma`. An attribute
  with no explicit role uses the field name.

Mesh draws use `MeshDescriptorIndices` from `utils.slang` (`mesh_submeshes`, `mesh_nodes`, `mesh_instances`, and
`mesh_transform` under `WBE_MESH_TRANSFORM`), plus `material_params` / `material_sampler`. `RTGraphicsDrawMeshVK` aliases the
ids the pipeline declares to the drawn mesh's members.

## Before changing support

Read [references/system-and-extension.md](references/system-and-extension.md) when the task asks what is supported, touches
the ACP shader compiler or serialized metadata schema, adds an annotation contract, changes resource-id resolution, or adds
a Vulkan descriptor type. Trace one real Slang declaration through raw reflection, normalization, deserialization, layout
collection, and its final consumer before editing.

Preserve the ACP compile profile, `spirv_1_6+spvDescriptorHeapEXT+vk_mem_model`, and do not enable unified descriptor-heap
stride. Never edit generated serialization files or compiled reflection fixtures; regenerate them through the build.

Render objects and Vulkan helpers are rendering-thread-confined. Route cross-thread updates through `IRenderer::add_job()`.

## Validate

```sh
python validation.py -t debug -- --gtest_filter='WBEShaderMetadataVKTest*:WBEShaderMetadataVKUtilsTest*'
```

Add `WBEPipelineDescriptorBindingsVKTest*` for binding or push-constant changes, `WBEMaterialTextureContractResolverTest*`
and `WBEMeshMaterialResourceBuilderVKTest*` for materials, `WBEMeshNodeContractResolverTest*` for mesh nodes, and
`WBEShaderRoleFieldResolverTest*` for role parsing. The wrapper then runs the full unit suite and the slangpy shader tests.
Do not run lint.

# Shader Reflection System and Extension Guide

## Pipeline map

1. A shader entry in an ACP manifest supplies `src`, `dst`, `metadata`, and optionally `stage` and `entry` (both or
   neither) and `macros` (`{ name, value? }`), which select shader variants.
2. `build_script/resource/acp/acp_compiler_shader.py` runs `slangc` from `PATH` with profile
   `spirv_1_6+spvDescriptorHeapEXT+vk_mem_model` (shaders under `sort/` or `pixel_sort/` temporarily omit `vk_mem_model`),
   include paths `res/assets/shaders` and `res/assets/shaders/utils`, `-DWBE_SHADER_DEBUG` when the target enables shader
   debug, `-g3` when tests are generated, and the resource's macros. Output is validated with `spirv-val`.
3. A second temporary compile with `WBE_REFLECTION_METADATA=1` lets reflection-only declarations contribute metadata
   without entering the runtime shader. Slang's JSON is normalized into the snake-case schema of
   `include/function/render/shader_metadata.hh`.
4. The loader stores the SPIR-V words and metadata path in `RUShader`; pipeline resources carry stage entry names
   (`src/function/render_vk/render_vk_resource_unit_loader.cpp`).
5. `ShaderMetadataLoader::load` resolves `RUShader::metadata_path`, parses the JSON, and deserializes `ShaderMetadata`.
6. Backend-neutral resolvers consume metadata. Pipeline compilers (`graphics_pipeline_compiler_vk.cpp`,
   `compute_pipeline_compiler_vk.cpp`) collect the descriptor-heap layout into `ROPipelineVK`; `pipeline_compiler_vk.cpp`
   only compiles and caches `ROShaderVK`. Ray-tracing pipelines get no reflected layout.
7. At graph build, a pipeline-run task's `PipelineDescriptorBindingsVK::bind_resources()` looks up each field's resource id
   through the task resource map and checks the graph resource's type UUID. `append_accesses()` reports the reflected
   accesses (samplers excluded). `push()` resolves the current frame slot, writes indices and push constants, and pushes the
   block.

Metadata is a sidecar resource. A missing shader handle or metadata path is an error.

## Normalized schema

The runtime model preserves global parameters; entry points, stages, entry-point parameters, results, and bindings;
declaration and type names; type kind, scalar type, array element count and element type; nested fields; semantics; binding
kind, index, count, space, offset, size, and element stride; and user attributes on parameters, entry points, types,
element types, and fields. Attribute arguments may be integers, floats, or strings; booleans fail normalization.

`RaytracingAccelerationStructure` normalizes to `kind == "resource"` and `scalar_type == "accelerationStructure"`. A
metadata-only declaration must still be instantiated as a global parameter to be queryable.

Compiled fixtures in tests are parsed directly from `test_env/res/assets/shaders/<dir>/<name>.json`.

## Vulkan descriptor-heap support

`src/function/render_vk/shader_metadata_vk_utils.cpp` parses `WBE_DESCRIPTOR` types through
`EnumReflection<ShaderDescriptorHeapTypeVK>` (canonical names with lowercase aliases). Each annotation takes two or three
string arguments: type, non-empty resource id, optional access. It must mark an aligned eight-byte `uint2` in the entry
point's only push-data parameter. The walker flattens nested structs and fixed-size struct arrays into paths such as
`lights[0].shadowmap`; paths identify layout fields, while binding uses resource ids.

Rejected with `std::runtime_error`: fewer than two arguments, unknown types, `write` on read-only types, misaligned or
overlapping fields, duplicate paths, more than one push-data parameter, a field carrying both `WBE_DESCRIPTOR` and
`WBE_PUSH_CONSTANT`, an indexed resource id outside a struct array, or a bare `[]`. A descriptor-free entry point yields an
empty layout. Graphics stages merge when equal paths agree on type, resource id, and offset; differing accesses combine to
`read_write`.

At record time an undeclared graph resource throws ("Render graph resource was not declared"), and an unresolved or null
slot throws at `push()`. Buffer bindings name a complete `ROBufferVK`; subranges are not supported.

`DescriptorHeapAllocatorVK` is generic (`create_heap`, `allocate_arena`, `allocate_descriptor`, `bind_heaps`).
`RendererVK::create_descriptor_heap_allocator()` defines the arenas: buffers (index 0 reserved for the shader-debug buffer),
sampled images, and storage images in the resource heap, and samplers in the sampler heap. Plugins add arenas;
`RayTracingPluginVK::configure_descriptor_heaps()` appends an acceleration-structure arena to the resource heap with the
device-reported acceleration-structure descriptor size as stride (at most `bufferDescriptorSize`, aligned to
`bufferDescriptorAlignment`). Arena indices are `byte_offset / stride + i`. `ROBufferVK`, `ROImageViewVK`, `ROSamplerVK`, and
`ROAccelerationStructureVK` lazily own their descriptor indices and defer release for frames in flight. The render graph
binds both heaps on every task command buffer.

## Built-in annotation contracts

All are declared in `res/assets/shaders/utils/utils.slang`.

### `WBE_DESCRIPTOR`

See above. Per-array-instance ids: an id ending in `[]` becomes `<name>[i]` for element `i` of the nearest enclosing
descriptor struct array; multiple fields may share one id.

Ray tracing keeps the metadata declaration and runtime index distinct:

```slang
struct DescriptorIndices {
    [WBE_DESCRIPTOR("acceleration_structure", "acceleration_structure")]
    uint2 scene;
};

#ifdef WBE_REFLECTION_METADATA
RaytracingAccelerationStructure scene_acceleration_structure;
#endif
```

Files: `shader_metadata_vk_utils.{hh,cpp}`, `pipeline_descriptor_bindings_vk.{hh,cpp}`, `descriptor_heap_allocator_vk.cpp`,
`ray_tracing_plugin_vk.cpp`, `ro_acceleration_structure_vk.hh`, fixture `res/assets/shaders/descriptor_heap_reflection`.

### `WBE_PUSH_CONSTANT`

Marks a four-byte scalar in the push-data block. `"time"` requires `float` (renderer time, `double` → `float`);
`"frame_slot"` requires `uint`. Other identifiers are custom `uint`, `int`, or `float` constants
(`ShaderDefaultPushValueVK::CUSTOM`), recorded in `ShaderDescriptorHeapLayoutVK::default_fields`. Compute tasks
(`RTRunComputePipelineVK`, `RTRunComputePipelineIndirectVK`, `RTRunComputePipelinesSequentialVK` with a per-entry overload,
`RTPingPongVK`) expose `set_push_constant()` (throws on an undeclared name or type mismatch) and `try_set_push_constant()`
(returns `bool`); values persist across pushes. Their recipe params take a `push_constants` table of identifier → number.
Graphics tasks have none. Recording crashes if a custom constant was never set.

### `WBE_VERTEX_INPUT`

`VertexInputContractResolver` searches entry-point parameter fields recursively; an omitted role defaults to the field name,
and annotated fields must be `varyingInput`. Formats: scalar or vector `float32`, `int32`, `uint32` with one to four
components. Roles are matched against a span of `RUVertexView` bindings: each role must be provided by exactly one binding
with the same format, only bindings actually read become Vulkan bindings (renumbered from 0, with `source_bindings` mapping
back), and `resolve_requirement()` / `ROGraphicsPipelineVK::get_vertex_input_requirement()` expose a mesh-independent
`VertexInputRequirement`. Fixture: `res/assets/shaders/vertex_input_attributes`.

### `WBE_TEXTURE` and the material contract

`MaterialTextureContractResolver` reads direct children of `MaterialParams`. Each annotated index is a four-byte `int32`
scalar; roles are unique by name and hash and come from the shader.

- `material_textures` (metadata-only) is a `parameterBlock` of `MaterialTextureContractMetadata` with `material_sampler`
  (sampler state) and `textures` (texture array).
- `material_params` is a constant buffer containing only `MaterialParamsBuffer.materials`, a sized array
  (`WBE_MAX_MATERIAL_COUNT`) with reflected offset, size, and stride.
- `resolve()` returns `std::nullopt` when neither is declared; stages must agree, and both must appear together.

Applications configure only the two base variable names (graphics pipeline compiler context) and
`MeshCompilerVK::Config::default_material_resource_key`. `MeshCompilerVK` compiles shader-independent mesh resources;
material tables are built lazily per distinct contract by `MeshResourcesVK::acquire_material_table()` through
`MeshMaterialResourceBuilderVK`, mapping each texture to `ROImageViewVK::get_sampled_view_index()`.
`RTGraphicsDrawMeshVK::prepare_graph()` triggers this. Upstream material shaping belongs to `wbe-utils-model-compiler`.

### Mesh contract

`MeshDescriptorIndices` uses `mesh_submeshes`, `mesh_nodes`, `mesh_instances`, and `mesh_transform` (only with
`WBE_MESH_TRANSFORM`), with the reflection-only `MeshContractMetadata`. C++ names them `REND_RES_MESH_SUBMESHES`,
`_NODES`, `_INSTANCES`, `_TRANSFORM`, `_MATERIAL_PARAMS`, and `_MATERIAL_SAMPLER` (`mesh_resources_vk.hh`).
`RTGraphicsDrawMeshVK` aliases only the ids the pipeline declares and throws when `mesh_transform` is declared without
`params.transform`, or `material_params` without a material contract. `MeshNodeContractResolver` reads the `NodeData` stride
and `transform` / `normal_transform` offsets from the global `mesh` parameter.

### Other attributes

- `[WBE_DATA_TYPE("name")]` on structs is preserved in metadata; it has no C++ consumer beyond tests.
- `WBE_SHADER_DEBUG_ASSERT` writes `ShaderDebugError` into the shader-debug buffer at reserved descriptor index 0 when
  `WBE_SHADER_DEBUG` is defined.
- `software_mul_float4x4_vector` / `WBE_SOFTWARE_MATRIX_ROWS` work around whole-matrix loads through descriptor heaps; use
  them when reflected buffers carry matrices.

## How to expand reflection support

Trace the exact raw Slang JSON first; do not infer layout from Slang source alone.

1. Normalize the new property in the narrowest helper of `acp_compiler_shader.py`.
2. If runtime consumers need it, add a serializable field in `shader_metadata.hh`; let the build regenerate serialization.
3. Cover deserialization in `tests/unit/function/shader_metadata_vk_test.cpp` with a small normalized JSON and, when Slang
   behavior matters, a compiled fixture under `res/assets/shaders` reached from `res/res_chunks` (`scope = "test_only"`).
4. Put semantic validation in a backend-neutral resolver; use `ShaderMetadataQuery` for uniqueness and
   `ShaderRoleFieldResolver` for roles.
5. For a Vulkan descriptor, extend `ShaderDescriptorHeapTypeVK` and the field handling in `shader_metadata_vk_utils.cpp`,
   preserving alignment, overlap, path, resource-id, stage-merge, and array rules, and the binding / access logic in
   `pipeline_descriptor_bindings_vk.cpp`.
6. Add resolver tests for invalid type, layout, duplicate, missing-pair, and cross-stage mismatch cases.

Do not branch generic propagation on concrete texture roles, move backend descriptor concepts into `function/render`, or
catch engine exceptions.

## Known limitations

- No single API returns an arbitrary shader value; load metadata, query it, and let a consumer interpret it.
- Metadata is re-read from the sidecar on each load.
- Descriptor translation is Vulkan-specific; buffer bindings cover whole render objects.
- Acceleration-structure binding requires the ray-tracing plugin.
- Only fixed-size arrays of descriptor-bearing structs expand into indexed paths.
- `slangdconfig.json` gives the language server only `res/assets/shaders`, not `utils/`.

## Fixtures and tests

- Fixtures: `reflection_attributes`, `forward_cook_torrance_pbr` (material layout, mesh storage buffers, mesh nodes),
  `vertex_input_attributes`, `descriptor_heap_reflection`, `deferred_gbuffer` (masked entry).
- `shader_metadata_vk_test.cpp`, `shader_metadata_vk_utils_test.cpp`, `pipeline_descriptor_bindings_vk_test.cpp`,
  `shader_role_field_resolver_test.cpp`, `material_texture_contract_resolver_test.cpp`,
  `mesh_node_contract_resolver_test.cpp`, mesh material resource builder tests, all under `tests/unit/function/`.
- `tests/shader/test_*.py` with kernels in `tests/shader/kernels/`: slangpy tests of shader utility functions, run by
  `run_shader_tests.py` (and by `validation.py`).

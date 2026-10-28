# Asset Standard

Assets are described by TOML manifest files (`.toml`) in the `res` folder and processed by ACP during build time.
ACP emits each source manifest `<path>.toml` as the JSON runtime index file `<path>.json`; runtime code refers to the
`.json` path. Reflection runs before ACP, so ACP can type-check Teal scripts against the generated declarations.

## Manifest Discovery

ACP only starts from root manifests matching `res/res_chunks/**/*.toml`. It does not scan every TOML file under
`res`, because other files may also be TOML and must not be treated as manifests.

Root manifests may reference additional manifests with the `include` array. Include paths are resolved
relative to the resource root and must name `.toml` source manifests. ACP loads only those referenced manifest files from
the resource tree, then recursively follows their includes. Emitted runtime manifests list the corresponding `.json` paths.

TOML files outside `res/res_chunks/` are processed only when they are reachable through `include` from
a root chunk manifest. Included files must be valid ACP manifests with a `resources` array.

## Manifest Layout

Manifests are TOML 1.0 documents parsed with Python's `tomllib`. Top-level `include` is an array of manifest paths, and
each resource is one `[[resources]]` table. Nested resources use `[[resources.<field>]]` arrays of tables or inline tables:

```toml
include = ["assets/materials/default_material.toml"]

[[resources]]
id = "example_shader"
source_type = "shader"
src = "assets/shaders/example.slang"
dst = "assets/shaders/example.spv"
metadata = "assets/shaders/example.json"
```

A manifest with no resources still declares `resources = []`. TOML has no null; omit optional fields instead.

## Resource Scope

The optional `scope` field accepts `global` or `test_only`. The default is `global`.
Global resources are compiled into both deployment and test outputs. Test-only resources are
compiled only when tests are enabled. ACP validates scope values, filters entries before
compilation, and omits `scope` from runtime index files.

Set `scope` on an individual resource entry, or on a whole manifest when all its resources
and includes are test-only. A test-only manifest is skipped for deployment. A global
manifest may include a test-only manifest; ACP removes that include from deployment output.
For a nested resource, keep it in the same scope as its containing resource.

Source manifests live under `res/`. ACP writes deployment resources under `build/<target>/bin/res` and, for targets that
build tests, every resource (global and test-only) under `test_env/res`.

## Manifest Entry Rules

- Every resource entry declares its type: `source_type` for entries ACP compiles or copies from source files, or `type` for
  runtime entries ACP passes through. Nested resources are compiled only when they carry a `source_type`.
- Type-specific fields are allowed and preserved.
- File-backed resources (`audio`, `binary`, `font`, `image`, `text`, `video`) use `source_file`, a source path in `res`, and
  an optional output `path` (defaulting to the source path). ACP copies the file and strips the source fields.
- Missing files referenced by `source_file` are build errors.

### Source types

| `source_type` | Compiler | Runtime result |
| --- | --- | --- |
| `shader` | Slang to SPIR-V plus reflection metadata | `shader` (`path`, `metadata`) |
| `image_texture` | Image to KTX2 (`src`, `dst`, `source_format` / `target_format` among `rgb`, `srgb`, `bc5`, `bc7`, `sbc7`, `generate_mipmap`, optional `flip_h` / `flip_v`) | `texture` (`path`) |
| `model` / `static_geometry` | `wbe-utils-model-compiler` | meshes, materials, textures, skeletons, binaries |
| `mesh` | Hand-authored vertex and index data | `mesh` with binaries |
| `script` | Copies Lua; type-checks and compiles Teal | `script` (`path`) |
| `audio`, `binary`, `font`, `image`, `text`, `video` | File copy | same type |

### Runtime types

The engine loads `audio`, `binary`, `font`, `image`, `texture`, `text`, `script`, and `video` (core); `shader`,
`graphics_pipeline`, `ray_tracing_pipeline`, `material`, `mesh`, `skeleton`, and `sdf` (renderer); and `component`,
`prefab`, `world`, and `scene` (scene).

## Shader Resources

- `src`: source shader file in `res`.
- `dst`: compiled shader output path; emitted as the runtime `path`.
- `metadata`: reflection metadata output path.
- `macros`: optional compile-time Slang macro definitions. Each entry requires a non-empty `name` and may provide a string
  `value`.
- `stage` and `entry`: optional Slang stage and entry-point name; declare both or neither. Without them, every
  `[shader(...)]` entry point in the module is compiled.

Shader macros are compile-only and are not emitted into the runtime manifest. They select shader variants, such as the
`WBE_MESH_TRANSFORM` variants of mesh shaders:

```toml
[[resources]]
id = "example_shader_variant"
source_type = "shader"
src = "assets/shaders/example.slang"
dst = "assets/shaders/example_variant.spv"
metadata = "assets/shaders/example_variant.json"
macros = [
    { name = "WBE_MACRO_1" },
    { name = "WBE_MACRO_2", value = "1" },
]
```

## Pipeline Resources

A `graphics_pipeline` names each stage with `<stage>_stage_id` (a shader resource) or an inline `<stage>_stage`
(`{ path, metadata }`), plus `<stage>_stage_entry`, for the `vertex`, `tessellation`, `geometry`, and `fragment` stages. It
also sets `depth_test`, `depth_write`, and `cull_backfaces` (all default `true`) and `conservative_rasterization` (default
`false`). A `ray_tracing_pipeline` follows the same pattern for the `raygen`, `miss`, `closest_hit`, `any_hit`, and
`intersection_shader` stages (for example `intersection_shader_stage_id`). There is no compute-pipeline resource; applications compile compute pipelines from `shader` resources.

## Material Resources

Material resources fill shader-defined texture slots:

```toml
[[resources]]
id = "example_material"
type = "material"
textures = [
    { texture_role = "albedo", texture_id = "example_albedo" },
    { texture_role = "normal", texture_id = "example_normal" },
    { texture_role = "rma", texture_id = "example_rma" },
]
```

Materials do not declare a `material_type` or graphics pipelines; the graphics pipeline the application draws them with and its
reflected shader metadata define the rendering contract.

- An optional `pipeline_variant` (a non-empty string without NUL, `default` when absent) names which of a draw task's pipelines
  draws the material, for example `pipeline_variant = "foliage"`. Variant names are free strings: the recipe maps each to a
  compiled pipeline per pass (see `docs/renderer_vk/renderer_vk.md`, Drawing), so a material stays independent of passes and
  attachment formats.

- Each texture slot requires a non-empty `texture_role`. Shaders select roles through reflected `WBE_TEXTURE` attributes.
- The loader retains each exact texture role name together with its runtime hash; consumers use both and never treat a hash alone
  as identity. Resource units do not use closed role enums.
- A texture role may appear at most once in a material.
- Each slot provides exactly one of `texture` for an inline texture (`{ path }`) or `texture_id` for an existing `texture`
  resource (KTX2).
- Regular materials may omit slots; the corresponding shader-selected role comes from the configured default material.
- The default material must provide every texture role selected by each material shader used by the pass.
- `rma` is the canonical texture role and project abbreviation for the roughness, metallic, and ambient-occlusion channel pack.

Material texture descriptor-index fields are selected in `MaterialParams` with `WBE_TEXTURE`. An explicit string selects that
texture role. Omitting the argument selects the annotated field name:

```slang
[WBE_TEXTURE("albedo")]
int albedo_index; // Selects texture_role "albedo".

[WBE_TEXTURE]
int detail_index; // Selects texture_role "detail_index".
```

`MaterialTextureContractResolver` reflects these attributes independently for each graphics pipeline. A compiled mesh builds
one material table per distinct contract, resolving each texture to its global sampled-image descriptor-heap index and
writing it at the reflected material-field offset. The metadata-only `material_sampler` and `textures` members and the runtime
`materials` table remain fixed contracts in `utils/utils.slang`; applications may configure only the base variable names for
`material_textures` and `material_params`, plus the `default_material` resource key. Runtime shaders receive the material
parameter buffer and sampler through `WBE_DESCRIPTOR` push-data fields and access textures through the global heap.

`WBE_TEXTURE` and `WBE_VERTEX_INPUT` share the same open-role annotation resolver: both accept one optional string, default to the
annotated field name, retain exact names for diagnostics, and reject duplicate roles and distinct-name hash collisions. Their
contract policies remain separate: material textures validate the global descriptor/index-table shape, while vertex inputs
validate varying locations, scalar/vector formats, and resource byte layouts.

## Models and Meshes

`wbe-utils-model-compiler` owns final mesh and material shaping; `acp_compiler_model.py` only adapts ACP paths and schema.

- `model` takes `source_file`, `combine_nodes`, `flip_v`, `scale_vertex_pos`, `texture_output_dir`, `geometry_output_dir`,
  `source_space` / `target_space` (`{ up, right, front }`), and `texture_config` (`{ default, roles.<role> }`). Skinned models
  also produce a skeleton and joint-binding vertex attributes. Example: `res/assets/models/models.toml`.
- `static_geometry` takes a non-empty `source_files` array and rejects `combine_nodes`.
- A hand-authored `mesh` gives each submesh `vertices_data` and `indices_data` (position, normal, tangent, bitangent, uv, and
  optional joint bindings); ACP writes the binaries and an opaque submesh with one identity instance. Examples:
  `res/assets/meshes/cube.toml`, `triangle.toml`.
- `model`, `static_geometry`, and `mesh` accept `sdf_config = { enabled, resolution, data_structure }`. `enabled` defaults
  to false, and the other fields are read only when it is true: `resolution` is the number of samples per unit of distance,
  and `data_structure` must be `texture_3d`. ACP samples the signed distance to the mesh's triangles, placed by their
  instances, on a uniform grid over their bounds, enlarged to whole samples about its center with each sample at a cell
  center. It writes the samples to a `<mesh id>.sdf_texture` 3D KTX2 texture beside the geometry and emits an `sdf`
  resource `<mesh id>.sdf`, which the mesh references with `sdf_id`. Every submesh of a hand-authored `mesh` with an SDF
  must use inline geometry. The host `SDFCompilerMeshToSDF` samples with mesh-to-sdf's depth scans, rendered through EGL,
  and stores each distance rounded toward zero.

A runtime `mesh` has `opaque_instances`, `masked_instances`, and `double_sided_opaque_instances` categories. Each submesh
declares `material` (inline) or `material_id`, a required `aabb`, `vertex_bindings` (each a binary view with `stride` and
`attributes` of `role`, `type`, `offset`), `indices`, `first_instance`, and `instance_count`; instances carry a
`global_transform`. An optional `skeleton_id` references a `skeleton` resource (`joints = { binary_id, start, size }`), which
submeshes with `joint_binding_ids` / `joint_binding_weights` attributes require. An optional `sdf_id` references an `sdf`
resource in the mesh's space: `data_structure` (`texture_3d`: a 3D `VK_FORMAT_R16_SFLOAT` texture with one mip level
and one texel per sample), `texture_id`, and the `aabb` the samples cover, with each sample at the center of its texel's
cell. Distances are negative inside the surface.

## Script Resources

Scripts live under `res/assets/scripts/teal/` and are declared in `res/assets/scripts/teal/lua_scripts.toml`:

```toml
[[resources]]
id = "render_graph_utils.render_graph_utils"
source_type = "script"
source_file = "assets/scripts/teal/render_graph_utils/render_graph_utils.tl"
```

- A script's id is its module name: its path under `scripts/teal/` without its extension, with dots for slashes. ACP does not
  enforce this, but `require` resolves modules by id at runtime and by path in the Teal compiler and editor, so keep them in
  sync.
- `.lua` sources are copied. `.tl` sources are type-checked and compiled to `<name>.lua` with the vendored Teal compiler; a
  type error fails the build. Teal resolves `require` against the script root and the generated modules in
  `build/<target>/teal/` (`wbe.d.tl`, `render_graph_utils/render_graph.tl`, and
  `render_graph_utils/render_graph_tasks.tl`), so a Teal module can require a Lua module only through a `.d.tl` declaration
  next to it.
- A generated script module is a resource too. Its `source_file` is relative to `build/<target>/teal/`, where ACP looks
  first, and its entry sets `path` to keep the module's place under `assets/scripts/teal/`.
- Recipes go in `render_graphs/`; reusable graph parts and helpers go in `render_graph_utils/`, with features in
  `render_graph_utils/features/`. General script helpers remain in `utils/`.
  Test-only scripts go in `tests/` with `scope = "test_only"`.

At runtime `require` is the only import mechanism: `SMRResourceManager` resolves module ids to script resources, each module
loads once per script backend, and cyclic requires fail.

## Runtime Index Output

ACP generates runtime index files from manifests. Each manifest file produces a corresponding
index file in the output resource directory.

The engine runtime consumes generated index files using resource paths relative to `res`, with
`/` as path separator.

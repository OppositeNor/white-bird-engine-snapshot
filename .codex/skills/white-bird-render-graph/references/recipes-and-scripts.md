# Render Graph Recipes and Scripts

## Flow

1. The rebuild callback builds a `RendGraphRecipeImportsVK` with every resource (`add(name, bindable)`) and C++ task
   (`add_task(RendTaskDescVK)`) owned outside the graph.
2. `RendGraphScriptVK::run(key_or_source, context)` runs the recipe in a fresh sandboxed `SBLua` and returns its table as a
   `DataValue`. `require` resolves other script resources through `SMRResourceManager`.
3. `RendGraphRecipeBuilderVK().build(recipe, imports, render_context)` returns a `RendGraphDescVK`. It adds tasks through
   `RenderTaskRegistryVK::add_task()` (which runs the type's `prepare_graph`), wraps a `trigger` first and a `toggle`
   outermost, copies imported tasks, and builds subgraphs. A declared-but-missing or supplied-but-undeclared import or task
   is an error.
4. The callback may add to that descriptor for parts not yet expressed in Lua (the deferred test adds its acceleration
   structure and dynamic mesh entities). Keep everything else in the recipe.

Recipes only build descriptors; they never touch render objects.

## Handle-based recipes

Resource constructors return `graph_utils.Resource` handles. Pass image handles to `image_view`, handles in task aliases,
`graph:chain(handle, tasks)`, and subgraph port maps. `graph:build()` automatically collects declarations from those
references and image dependencies. Unused handles are omitted; shared handles emit once. A task's `uses` list retains
resources named directly by typed params or same-name reflected shader bindings, without declaring access or ordering.
Typed resource-id params use `handle.id`. Mesh/pipeline/task/event keys remain strings.

`import_resource(id)` names an exact C++ supplied import; `port(id)` names a local subgraph port; `reference(id)` names an
existing enclosing or composite member without declaring a new import. `graph:build(imports)` may preserve a complete
C++ supplied import list. Feature subgraphs return `graph_utils.Task`; the containing graph lowers them at build time.

`graph:add(description)` returns a task object. Save it and pass task objects to chains or call
`producer:connect(consumer, resource)`. Strings, forged handles, and handles from other graphs are rejected. Bind resource
names with `task:alias(local_name, resource)`: the alias is private to that task, duplicate names within one task fail,
and different tasks may reuse local names. Input alias maps and edge lists are copied per task. `alias` and `connect`
return the same task for fluent calls. Task IDs remain stable serialized names; task methods and graph references are
removed by `graph:build()`.

The emitted recipe retains the following plain data schema; handles and graph methods never cross the Lua/C++ boundary.

A recipe module exports `build(context)` returning `RendGraphRecipeVK` (`rend_graph_recipe_params_vk.hh`):

```lua
local type wbe = require("wbe")
local graph_utils = require("utils.render_graph")
local exports = {}

function exports.build(_context: wbe.RendRecipeContextVK): wbe.RendGraphRecipeVK
    local target = graph_utils.import_resource("swap_chain")
    local camera = graph_utils.import_resource("camera_info")
    local graph = graph_utils.graph()
    local params: wbe.RTParamsGraphicsDrawMeshVK = {
        mesh = "model_cube_static.mesh",
        pipeline = "draw_cube_unshaded_pipeline",
        parts = { "opaque" },
    }
    local draw = graph:add({
        id = "run_static_mesh",
        type = "graphics_draw_mesh",
        params = params,
        uses = { camera },
    })
    draw:alias("render_target_colors[0]", target)
    return graph:build()
end

return exports
```

- `imports`: names supplied by `RendGraphRecipeImportsVK`. Buffers that need initial data are application-owned imports,
  registered in the render database under their import name, because recipes cannot set initial data.
- `resources`: each `{ id, ... }` declares exactly one of `buffer`, `image`, `image_view` (`{ image, view }`), or `sampler`,
  using the reflected `Rend*ParamsVK` structs.
- `tasks`: `RendRecipeTaskVK` entries with `id`, `type`, `params`, `aliases`, `connect` (`{ to, resource }`), optional
  `trigger` (`{ event, activate }`), `toggle` (`{ event, enabled }`), `subgraph` and `ports`. Imported tasks set only `id`,
  `import = true`, and `connect`.
- `params` is untyped in `RendRecipeTaskVK`, so declare it with its reflected struct (`local params: wbe.RTParams...VK`).
  Params are strictly deserialized: unknown keys fail. Pipeline params are render-database keys; the application compiles
  those pipelines outside the callback and keeps them alive.
- Enums serialize as canonical names. Compare with `graph_utils.enum_name(value) == "CANONICAL_NAME"`, which also accepts
  the lowercase aliases.

## Context

`RendRecipeContextVK` holds `frames_in_flight`, `swap_chain_format`, and `swap_chain_extent`. `make_context()` builds the
plain one. An application that adds entries declares a `WBE_REFLECT_SCRIPT` struct deriving from it, assigns the base with
`get_recipe_context()`, and passes `serialize_context(context)` to `run()`; the recipe annotates
`build(context: wbe.<ContextType>)`. Size swap-chain-dependent resources and dispatches in Lua from `swap_chain_extent`.
Pass runtime state that rebuilds must preserve (toggle states, feature parameters) through the context.

Examples: `tests/executable/function/render/pixel_sorting_recipe_context.hh`,
`deferred_test/deferred_recipe_context.hh`.

## Triggers and toggles

- `trigger = { event, activate }` wraps the task with `RTTriggeredVK`, which records its child while it has remaining
  frames. The application emits `RTTriggeredVK::Command::trigger()` or `enable_for(n)` through `EventManager::emit(event,
  command)`. Run-once work (a static build) is a trigger activated while still pending; the application emits
  `enable_for(0)` after the first recorded frame, so later rebuilds create it inactive.
- `toggle = { event, enabled }` wraps it with `RTToggleVK`, outside any trigger, recording every frame while enabled
  (`Command::enable()` / `disable()`). Rebuilds re-run the recipe, so pass the toggle's current state through the context.
- A trigger or toggle on a subgraph task wraps every member.

`EventManager` lives in `function/event/event_manager.hh`; CPU readback tasks also emit their results through it under the
source resource id.

## Imported C++ tasks

Build a `RendTaskDescVK` with its factory and aliases, supply it with `imports.add_task()`, and list it in the recipe as
`{ id = <task id>, import = true }`. Imported tasks set no `type`, `params`, `aliases`, `trigger`, or `toggle` in the
recipe. Examples: `tests/executable/utilities/rt_imgui_vk.hh`, `deferred_render_graph.cpp`, `vulkan_pixel_sorting_test.cpp`.

## Feature subgraph modules

A reusable recipe part lives in `render_graphs/features/` and exports `subgraph(id, params, target)` returning a subgraph
task built with `graph_utils.subgraph(id, recipe_with_ports, { port = resource })`. Its C++ side provides
`configure_recipe(DataValue&, RendGraphRecipeImportsVK&)`, which serializes its `WBE_REFLECT_SCRIPT` params struct (derived
from `RecipeFeatureParams`, including the `module` name) into the context and supplies its resources and C++ tasks. The recipe
calls `require(params.module).subgraph(id, params, target)`.

Examples: `features/pixel_sort.tl` with `deferred_test/pixel_sort.{hh,cpp}`, `features/voxel_svo.tl` and
`features/voxel_octree.tl` with the deferred test's voxel features, and `render_graphs/deferred.tl`.

## Adding a task type

1. Write `include/function/render_vk/rt_params_<name>_vk.hh` with
   `struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION, WBE_REFLECT_SCRIPT) RTParams<Name>VK`; mark required
   fields `WBE_REFLECT_REQUIRED` and reference pipelines by render-database key. Mark fields that hold resource ids
   `WBE_RECIPE_RESOURCE(graph)`, or `WBE_RECIPE_RESOURCE(task_local)` for ids the task's aliases map, so the recipe
   constructor also accepts resource handles there.
2. Mark the task `WBE_META(WBE_RENDER_TASK(<method>), WBE_SERIALIZATION_ALIASES(<name>))`, or with a bare
   `WBE_RENDER_TASK` to register it without a recipe constructor, and declare
   `WBE_META(WBE_REFLECT) static Unique<IRenderTaskVK> create(const RenderContextVK*, const RTParams<Name>VK&, const RendGraphContextVK&)`.
   Add `static void prepare_graph(RendGraphDescVK&, RendTaskDescVK&, const RTParams<Name>VK&, const RenderContextVK*)` if
   the task must add imports or aliases.
3. Use `render_task_create_utils_vk.hh` (`acquire_task_compute_pipeline_vk`, `acquire_task_graphics_pipeline_vk`,
   `resolve_task_push_constants_vk`) in `create()`.
4. Rebuild. `render_task_registry.gen.cpp` registers the class name and aliases; a missing `create` or duplicate name fails
   the build. The params type appears in `wbe.d.tl` for Teal, and `graph:<method>(id, params, options?)`, which creates
   the type `<name>`, in the generated `utils.render_graph_tasks`.

Examples: `rt_full_memory_barrier_vk.hh` (simple), `rt_run_compute_pipeline_vk.hh` (standard), `rt_graphics_draw_mesh_vk.hh`
(with `prepare_graph`), and `tests/unit/function/render_task_registry_vk_test_tasks.hh` (test probes).

## Scripts and Teal

- Sources live under `res/assets/scripts/teal/` and are `script` resources declared in `lua_scripts.toml`: recipes in
  `render_graphs/` (`deferred.tl`, `draw_cube_unshaded.tl`, `pixel_sorting.tl`), features in `render_graphs/features/`,
  helpers in `utils/` (`render_graph.tl`, `math.tl`, `sort.tl`). Test-only scripts (`tests/teal_math.tl`,
  `tests/teal_recipe.tl`) use `scope = "test_only"`.
- A script's resource id is its module name: path under `scripts/teal/` without extension, dots for slashes. Keep them in
  sync so `require` resolves the same module at runtime and in the editor.
- `WBEACPCompilerScript` copies `.lua` sources and type-checks and compiles `.tl` sources with the vendored compiler; a type
  error fails the build. A Teal module requires a Lua module only through a `.d.tl` declaration next to it.
- Import engine types as types only (`local type wbe = require("wbe")`); declare data C++ hands a recipe as
  `WBE_REFLECT_SCRIPT` structs rather than Teal records.
- `require` is the only import mechanism; modules load once per backend and cyclic requires fail. New import sources are
  `IScriptModuleResolver`s whose `try_resolve()` must not throw.

`utils.render_graph` provides `Resource`, `Task`, `Graph`, `graph()`, `enum_name`, `buffer`, `storage_buffer`, `image`,
`storage_image`, `image_view`, `clamp_to_edge_sampler`, `import_resource`, `port`, `reference`, `indirect_dispatch`, and
`subgraph`. Graph colon methods are `:add`, `:get`, `:chain(resource, tasks)`, and `:build`. `utils.sort` provides sized
`radix_sort_buffers` handles and a `radix_sort_task`; `utils.math` provides `ceil_div` and `clamp`.

## Tests

- `tests/unit/function/rend_graph_recipe_builder_vk_test.cpp`: imports, tasks, subgraphs, triggers, toggles.
- `rend_graph_script_vk_test.cpp`: runs `tests.teal_recipe` and the shipped recipes with their features.
- `render_task_registry_vk_test.cpp`, `render_task_registry_vk_builtin_test.cpp`: registration, params, push constants.
- `rt_triggered_vk_test.cpp`, `rt_toggle_vk_test.cpp`, `rt_graphics_draw_mesh_vk_test.cpp`, `teal_declarations_test.cpp`.

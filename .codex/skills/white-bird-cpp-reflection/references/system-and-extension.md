# C++ Reflection System and Extension Guide

## System boundary and flow

1. `build.py` collects project `.hh` headers (excluding dependencies, generated output, caches, and build directories).
   `tests/` is scanned only for targets with `generate-tests`.
2. `build_script/on_build.py` constructs `WBEMetaparser` and `WBEReflector`. Reflection runs before the ACP.
3. `build_script/reflection/metaparser.py` runs Clang under `WBE_REFLECTION_PARSER`, where `WBE_META` becomes an `annotate`
   attribute. It records, per header and only for what that header declares: classes, base specifiers, `WBE_REFLECT`
   fields, `WBE_REFLECT` methods (name, arguments, return type, static, const), annotated enums with enumerators, values,
   attributes, and aliases, labels, type spellings, and annotation strings (`metadata_types.py`).
4. Metadata is ordered by include-dependency depth. `WBEReflector` checks label redefinition, enum qualified-name
   redefinition, and label hash collisions, then renders every `generate.toml` entry.
5. Generated headers go to `include/per_target/<target>/generated/`, sources to `src/per_target/<target>/generated/`.

`include/core/reflection/reflection_defs.hh` holds only the `WBE_META` / `WBE_LABEL` macros and optional helpers. Never write
runtime logic that inspects `WBE_META`.

### Metadata cache

The cache is one JSON per header at `build/<target>/build_cache/source_metadata/<sha256(path)>.json`. A header is reparsed
when its content hash changes or the parser signature changes; the signature is `METAPARSER_VERSION` plus the Clang
arguments. Bump `METAPARSER_VERSION` in `metaparser.py` whenever extraction changes. `code_gen.py` writes an output only when
its rendered content changes.

## Annotation categories

`gen_info` in `build_config.py` defines the category lists. Derived memberships: `serializables` are also
`static_serializables`; `reflect_script` classes join `static_serializables` and script enums join `reflected_enums`.

| Annotation | Category | Generates |
| --- | --- | --- |
| `WBE_SERIALIZABLE`, `WBE_CONFIG_OPTION` | serializables | SD, Access, virtual `DataValue` methods (`serializable_impl.gen.cpp`), factory registration |
| `WBE_SERIALIZABLE_STATIC`, `WBE_COMPONENT` | static_serializables | SD and Access |
| `WBE_STRICT_DESERIALIZATION` | strict_deserializations | strict `deserialize`; effective only with a static-serializable category |
| `WBE_REFLECT_ENUM` / `WBE_REFLECT_ENUM_IGNORE` | reflected_enums | `EnumReflection<E>` and enum `SerializableSD` |
| `WBE_REFLECT_SCRIPT` | reflect_script | Teal declaration plus SD/Access or `EnumReflection` |
| `WBE_RENDER_TASK`, `WBE_RENDER_TASK(<method>)` | render_tasks | `render_task_registry.gen.cpp`; with a method, its recipe constructor in `build/<target>/teal/render_graph_utils/render_graph_tasks.tl` |
| `WBE_CLA_CONFIGURATION` | cla_configurations | `CLAConfiguration<T>` |
| `WBE_LABEL(name, CATEGORY)` | static/dynamic labels | `LabelName<hash>`, `LabelManager` |
| any annotated class | — | `TypeUUID<T>`, `TypeUUIDManager` (enums excluded) |

Field and method annotations: `WBE_REFLECT`, `WBE_REFLECT_REQUIRED`, `WBE_SERIALIZATION_ALIASES(...)`, and on render-task
params fields that hold resource ids, `WBE_RECIPE_RESOURCE(graph)` or `WBE_RECIPE_RESOURCE(task_local)`. An attribute is a
bare name or `NAME(<parameters>)`; reflection records the trimmed parameters by attribute name. Assignment syntax is an
error. Methods are recorded for every consumer, but only the render-task registry and the recipe constructors use them
today (both read `create`).

### Aliases

Field deserialization checks the canonical key before aliases. Class aliases feed the factory, type-name registry, and
render-task registry; enumerator aliases add extra names. Uniqueness is enforced per consumer: the factory throws at
runtime, enums and render tasks fail the build with `#error`, and `TypeUUIDManager` silently overwrites, so keep names
globally unique. The alias parser accepts a comma-separated list of names of letters, digits, and underscores.

## Generated outputs

Every `generate.toml` in the repo is used (except under `.cache`, `.git`, `.github`, `__pycache__`, `build`). Its `[[generate]]` entries take
`out_dir` (empty means the manifest's own directory), `output_name`, `template`, `data`, and optional `params`. `${name}` resolves
from the entry's `params`, then built-ins (`build_target`, `build_dir`, `binary_dir`, `root_dir`, `include_dir`,
`source_dir`), then environment variables; an unresolved name is an error. A `data` string value starting with `.` is
replaced by the reflection attribute named by its data **key** (the `gen_info` lists plus `metadata`); keys starting with
`_` are rejected.

| Output | Template | generate.toml |
| --- | --- | --- |
| `serializables_sd.gen.hh` | `serializables_sd.hh.jinja` | `include/per_target/` |
| `enum_reflection.gen.hh` | `enum_reflection.hh.jinja` | `include/per_target/` |
| `serializables_access.gen.hh` | `serializables_access.hh.jinja` | `include/per_target/` |
| `serializable_factory.gen.hh` | `serializable_factory.hh.jinja` | `include/per_target/` |
| `label_manager.gen.hh` | `label_manager.hh.jinja` | `include/per_target/` |
| `type_uuid.gen.hh` | `type_uuid.hh.jinja` | `include/per_target/` |
| `cla_configuration.gen.hh` | `cla_configuration.hh.jinja` | `include/per_target/` |
| `serializable_impl.gen.cpp` | `serializable_impl.cpp.jinja` | `src/generated/` |
| `render_task_registry.gen.cpp` | `render_task_registry.cpp.jinja` | `src/generated/` |
| `build/<target>/teal/wbe.d.tl` | `teal_types.d.tl.jinja` | `src/generated/` |
| `tlconfig.lua` (repo root, gitignored) | `tlconfig.lua.jinja` | `src/generated/` |
| `executable_test.gen.cmake` | `executable_test.cmake.jinja` | `tests/executable/function/render/` |

### Serialization (`serializables_sd.gen.hh`)

The primary `SerializableSD<T>`, and the `std::optional` and `std::vector` specializations, are hand-written in
`core/reflection/serializable_sd.hh`. The generated header emits one `SerializableSD<T>` per static-serializable class with
`serialize`, `deserialize`, and `deserialize_fields`. Serialization visits recorded bases, then fields. Deserialization of a
base calls `SerializableSD<Base>::deserialize_fields`, so a hand-written base specialization must provide `serialize` and
`deserialize_fields`. `extended_parents` records every base specifier, reflected or not.

### Strict deserialization

A strict class's `deserialize` first calls `reject_unknown_serializable_keys()` with all field names and aliases of the class
and its reflected bases, throwing `Unknown field '<key>' for <T>.`. `deserialize_fields` is the lenient path used for bases.
A strict class accepts its lenient base's fields; a strict base used alone rejects a derived class's keys; a strict member
type is checked inside a lenient holder. It applies to any `DataValue` source: JSON, TOML, or script.

### Factory, access, UUIDs

`SerializableFactory` takes `const DataValue&` and builds `Unique<Serializable>`, `Ref<Serializable>` (global allocator), or
an in-place `Serializable*` in caller-owned storage; the factory singleton must be initialized. `SerializableAccess<T>` lists
direct reflected fields only (not inherited ones), and the requested type must match Clang's recorded spelling exactly.
`TypeUUID<T>::UUID` is computed from the canonical class name.

### Teal declarations (`wbe.d.tl`)

Everything sits in `local record wbe ... return wbe`, with built-in `Vec2`, `Vec3`, `Vec4`, `Quat`. Script classes are
`interface`s that `is` their reflected script bases (non-reflected bases are dropped). A script field or base naming any
`WBE_META`-annotated type that is not `WBE_REFLECT_SCRIPT` raises `ValueError` (`build_script/reflection/teal_types.py`).
Type mapping: `std::vector`/`Vector` → `{T}`, `std::optional<T>` → `T`, strings and `Buffer` → `string`, integers →
`integer`, floats → `number`, `glm::mat4` → `{number}`, `DataValue` and unknown types → `any`, script enum fields →
`Enum | integer`. Field aliases are emitted as extra fields, and keywords are quoted (`["type"]`). Script enums list canonical
names and aliases, skipping ignored enumerators. The unit test reads the file through `WBE_TEAL_DECLARATIONS_DIR`.

## Supported field types

`DataValue::set/get` (`core/data/data_value.hh`) supports `DataValue`, `std::nullptr_t`, `bool`, all integral and
floating-point types (unsigned values above `INT64_MAX` throw on set), raw enums, `std::string`, `Buffer` (an overlong string
throws on read), `glm::vec2/vec3/vec4/quat` as x/y/z/w maps (`vec2` also reads u/v), `glm::mat4` as 16 column-major floats,
and `std::vector` of supported types. Through `SerializableSD`: `std::optional<T>`, reflected enums, and nested reflected
classes. Integer/float reads convert when exact; an empty list and an empty map read as each other.

A nested type is serializable only if it belongs to a static-serializable category or has a visible hand-written
`SerializableSD` specialization. Vectors and optionals default-construct elements during deserialization.

## Extending

### A type or container

- Add a hand-written `SerializableSD<NewType>` in a non-generated header visible before generated code uses it, or
- extend `DataValue::set/get` when the value tree itself must understand the type. Parsers change only when a new
  `DataValueKind` is added.

Test round trips, missing values, type mismatches, nested usage, and strictness.

### Metadata or a language construct

1. Extend `metadata_types.py`.
2. Populate it in `metaparser.py` from verified Clang cursor shapes, and bump `METAPARSER_VERSION`.
3. Preserve cache serialization.
4. Add a representative annotated fixture in `tests/unit/core/reflection_test/reflection_test_data.hh`.
5. Update only consumers that need it.

### A category or consumer

Add the category to the right `gen_info` set and template conditions; extend the metaparser only for structured parameters
or different AST extraction. For a new consumer, add a focused Jinja template and a `generate.toml` entry, reuse existing
metadata, keep generated includes sufficient for every emitted type spelling, and let `code_gen.py` manage writes.

## Examples and tests

- `tests/unit/core/reflection_test/reflection_test_data.hh`: inheritance (multiple, diamond), required fields, aliases,
  static and strict types, reflected methods, enums.
- `include/function/render_vk/rt_params_run_compute_pipeline_vk.hh` and `rt_run_compute_pipeline_vk.hh`: params struct and
  render task.
- `include/function/render_vk/mesh_resources_vk.hh`: script enum with lowercase aliases.
- `include/function/render_vk/vulkan_enums_vk.hh`, `rend_*_params_vk.hh`: reflected Vulkan enums and resource params.
- `tests/executable/function/render/pixel_sorting_recipe_context.hh`: script context deriving from `RendRecipeContextVK`.
- `include/core/engine_config/engine_config_options.hh`: config option with CLA configuration.
- Tests: `tests/unit/core/reflection_test/` (aggregated by `reflection_test.cpp`), `tests/unit/core/script_test/`,
  `tests/unit/core/parser_test/`, `tests/unit/function/teal_declarations_test.cpp`,
  `render_task_registry_vk_test.cpp`, `render_task_registry_vk_builtin_test.cpp`, `rend_resource_params_vk_test.cpp`,
  `rend_graph_script_vk_test.cpp`.

## Invariants

- Do not confuse this system with Slang shader reflection.
- Do not edit generated artifacts. Unannotated fields and methods are invisible.
- Recorded type spellings are emitted into generated code and must compile there.
- Reflection and parser errors are `std::runtime_error` (or build failures) and are not caught outside tests.
- Keep all engine and test-support types in `WhiteBirdEngine`.

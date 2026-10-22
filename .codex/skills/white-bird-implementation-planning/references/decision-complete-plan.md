# Decision-Complete Plan Reference

Use this reference for substantial plans that span multiple files, layers, generated artifacts, external libraries, or runtime lifecycles.

## Recommended structure

### Summary

- State the user-visible or architectural outcome.
- Name the selected approach and important boundaries.
- Call out preserved behavior when it prevents an unintended migration or refactor.

### Public contracts

- Give exact manifest or configuration shapes when they change.
- Name added, changed, and removed C++ or Python APIs and resource types.
- Define references, identifiers, return values, error behavior, and compatibility policy.
- Distinguish offline/compiler-only concepts from serialized runtime state.

### File and symbol changes

Write each entry in this form:

```text
path/to/file — ClassName::function_name()
```

For each entry, specify the exact responsibility after the change. Include constructor injection, call-site migration, removed helpers, cache keys, data conversion, ownership transfer, and teardown ordering when applicable. List new files separately from edits when that makes ownership clearer.

Do not stop at a filename when the file contains multiple plausible integration points. Do not list a file unless the described change is required.

### Build integration

Specify, as applicable:

- exact CMake options and `add_subdirectory` placement;
- target names, visibility, and dependency edges;
- configure, prerequisite-build, generation, asset-conditioning, and final-build order;
- generated artifact destinations and how later phases locate them;
- include paths needed by compilation or reflection;
- shared-library output, RPATH, DLL lookup, or packaging behavior;
- why an upstream build target is used or deliberately avoided.

Account for tools needed before the main build. If a compiler or binding is consumed by asset conditioning, ensure the plan builds it before asset conditioning runs.

### Runtime flow and lifecycle

Trace data from producer to final consumer. Specify:

- validation and failure boundaries;
- caching, deduplication, sharing, and identity keys;
- thread, worker, command-buffer, or queue synchronization;
- resource construction, adoption, and destruction order;
- deferred destruction and shutdown behavior;
- which library or layer is the authoritative source of metadata.

Prefer established library helpers when they safely replace engine-owned parsing, allocation, upload, or synchronization. Record important costs or limitations, such as synchronous submission or allocator incompatibility.

### Tests and acceptance criteria

Name the exact existing test files to migrate and new test files to add. Cover:

- valid behavior and representative data flow;
- invalid schemas, unsupported inputs, and library failures;
- ownership, caching, sharing, and teardown;
- build-order and artifact-discovery behavior when practical;
- compatibility boundaries and deliberately unchanged behavior.

Give the repository-approved focused validation command followed by the full validation command. Avoid duplicating very large fixtures when a small purpose-built fixture proves the same contract.

### Assumptions

Record defaults selected because the user did not specify them, deliberate deferrals, and external-library constraints that shaped the design. Do not repeat facts already fixed by the requested contract.

## Final review

Before presenting the plan, verify that an implementer will not need to decide:

- where a change belongs;
- which symbol owns the behavior;
- the public API or serialized shape;
- how data reaches its consumer;
- how resources are shared and destroyed;
- when prerequisites are built and located;
- what errors are expected;
- which tests must change and how success is validated.

Also verify that the plan does not execute the work, modify unrelated systems, contradict `AGENTS.md`, edit generated files directly, or duplicate instructions already supplied by a more specific domain skill.

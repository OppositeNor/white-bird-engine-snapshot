---
name: white-bird-implementation-planning
description: Produce decision-complete implementation plans for White Bird Engine. Apply when a user asks to plan or design a non-trivial engine change, revise an implementation plan, or map proposed work to concrete repository files, symbols, build stages, lifecycle behavior, and validation. Do not invoke for a direct implementation request unless planning is explicitly requested or the active mode requires a plan.
---

# White Bird Implementation Planning

Produce a plan that another engineer or agent can implement without making unresolved architectural, API, ownership, build-order, or test decisions.

## Ground the plan in the repository

Read the active `AGENTS.md` and any domain skill that applies to the requested subsystem. Inspect the repository before asking the user about anything discoverable there.

For a non-trivial change, trace the relevant path end to end:

- authored input or public API;
- compiler, generator, or transformation stages;
- runtime representation and consumers;
- ownership, caching, synchronization, and destruction;
- build configuration, prerequisite artifacts, and runtime discovery;
- generated artifacts and their order: reflection (`*.gen.*`, `wbe.d.tl`) runs before the ACP, which compiles shaders,
  models, and Teal scripts, before CMake;
- for rendering work, whether the change belongs in a Teal recipe or feature module, a registered task type and its
  `RTParams` struct, the rebuild callback, or code outside the callback;
- tests, fixtures, migrations, and validation entry points.

Inspect declarations and definitions together. Check `git diff` when the request concerns work in progress, and preserve unrelated changes. Resolve repository facts through searches and file inspection; ask the user only about choices that materially change the result.

## Make the plan decision complete

State the outcome and architectural approach first. For every non-trivial edit, identify:

- the exact file path;
- the class, function, method, or configuration block;
- whether it is added, changed, removed, or intentionally preserved;
- the resulting contract and behavior, including errors and ownership where relevant.

Be equally concrete about build integration. Name targets, dependencies, options, artifact locations, build ordering, module/library discovery, and deployment behavior. Do not say merely “update CMake” or “add tests.”

Explicitly identify behavior that stays unchanged when doing so prevents accidental scope expansion. Separate confirmed repository facts from assumptions and user-selected policy. Do not invent unresolved APIs; ask for a decision or record a justified default.

Scale the plan to the task. A small local edit needs only a compact file/symbol plan. Cross-layer resource, compiler, renderer, lifecycle, or build changes require the full end-to-end treatment.

## Present and revise

Group the plan by subsystem and execution dependency rather than presenting an unstructured file inventory. Include public contracts, implementation changes, tests, validation commands, and assumptions when those categories matter.

When the user revises a decision, return a complete replacement plan with the obsolete design removed. Follow the active interaction mode's required plan wrapper or formatting.

Before finalizing a substantial plan, read [references/decision-complete-plan.md](references/decision-complete-plan.md) and apply its checklist.

# White Bird Engine

> A free game engine built for rhythm game development.
>
> Licensed under [Apache License Version 2.0](https://github.com/OppositeNor/white-bird-engine/blob/main/LICENSE).

- [简体中文](./README.zh-Hans.md)

## How to build

Clone the repository:

```sh
git clone https://github.com/OppositeNor/white-bird-engine.git --recursive
cd white-bird-engine
```

Or:

```sh
git clone https://github.com/OppositeNor/white-bird-engine.git
cd white-bird-engine
git submodule update --init --recursive
```

Install build dependencies:

```sh
sudo pacman -S cmake clang ninja
```

Install [Miniforge](https://github.com/conda-forge/miniforge) to manage the build environment with Conda.

### Setting up build environment

From the repository root, create and activate the Linux build environment:

```sh
conda env create -f environment.yml
conda activate white-bird-engine
```

To update an existing environment:

```sh
conda env update -n white-bird-engine -f environment.yml
conda activate white-bird-engine
```

`environment.yml` installs the Python dependencies and requests `libstdcxx-ng>=16` from conda-forge.
The runtime provides `GLIBCXX_3.4.35`, required by the native model compiler when built with the current system toolchain.
Updating the environment resolves the older Conda `libstdc++.so.6` mismatch without an `LD_PRELOAD` workaround.

The build script builds the local `wbe-utils-model-compiler` and pyktx bindings automatically; no separate pip installation is
needed. Keep the environment activated when running `build.py`, `./build-debug`, or `validation.py`.

The C++ runtime is licensed under GPLv3 **with the GCC Runtime Library Exception 3.1**. For ordinary GCC/Clang builds,
this exception permits linking the runtime while keeping this project's Apache-2.0 license. Redistributing the runtime
library itself still requires complying with its license, including applicable notices and source requirements.
See the [GCC runtime licensing FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html).

### Build

```sh
python ./build.py
```

You can also indicate the build target.

```sh
python ./build.py -t debug          # Debug target
python ./build.py -t release        # Release target
python ./build.py -t deploy         # Deploy target (default)
python ./build.py -t debug-gcc      # Debug target with GCC compiler
python ./build.py -t release-gcc    # Release target with GCC compiler
python ./build.py -t deploy         # Deploy target with GCC compiler
```

### Static model categories

Static models in `res/assets/models/models.json` use `source_files` to combine models into shared geometry.
The compiler emits one `<id>.mesh` resource with `opaque_instances`, `masked_instances`, and `double_sided_opaque_instances`
categories. Opaque glTF materials with `doubleSided: true` enter the third category. The deferred test draws this category
with its own pipeline, without face culling, and reverses back-face
lighting normals. See the [model compiler documentation](dependencies/wbe-utils-model-compiler/README.md) for the manifest contract.

## Contribution

### Use of AI

Use of AI assistants is allowed and encouraged for this project. At the start of every new AI session, please have the
agent read [AGENTS.md](./AGENTS.md) first. That file is a compact, high-density reference written
specifically for AI agents and covers the project's layered architecture, build workflow, test layout, memory model, and
naming rules.

After the AI finishes editing, **carefully review** every change and make sure you understand exactly what it did before
committing.

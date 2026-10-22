# Copyright 2025 OppositeNor
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
from pathlib import Path
from typing import Callable

import pytest
import slangpy as spy

import build_config


SHADER_INCLUDE_DIR = Path(build_config.root_dir) / build_config.resource_dir / "assets" / "shaders"
# The same include paths the ACP shader compiler passes to slangc.
SHADER_INCLUDE_PATHS = [SHADER_INCLUDE_DIR, SHADER_INCLUDE_DIR / "utils"]
KERNEL_DIR = Path(__file__).parent / "kernels"


@pytest.fixture(scope="session")
def shader_include_dir() -> Path:
    """Shader source root, resolved from the resource directory and used as the Slang include path."""
    return SHADER_INCLUDE_DIR


@pytest.fixture(scope="session")
def device(shader_include_dir: Path) -> spy.Device:
    """Vulkan device shared by all shader tests, with the engine shader include paths."""
    return spy.create_device(type=spy.DeviceType.vulkan, include_paths=SHADER_INCLUDE_PATHS)


@pytest.fixture(scope="session")
def load_kernel(device: spy.Device) -> Callable[[str, dict[str, str | int]], spy.ComputeKernel]:
    """
    Loader for the test kernels under tests/shader/kernels.

    Each call compiles the kernel's `main` entry point in a new Slang session whose preprocessor defines are the given
    ones, so kernels can be specialized per test.
    """
    def load(p_kernel_name: str, p_defines: dict[str, str | int]) -> spy.ComputeKernel:
        session = device.create_slang_session(compiler_options={"include_paths": [*SHADER_INCLUDE_PATHS, KERNEL_DIR],
            "defines": {name: str(value) for name, value in p_defines.items()}})
        module = session.load_module(p_kernel_name)
        return device.create_compute_kernel(session.link_program([module], [module.entry_point("main")]))
    return load

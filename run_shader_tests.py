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
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
from typing import TextIO

import build_config
from build_script.build_dependency.build_dependency_slangpy import WBEBuildDependencySlangPy


ROOT_DIR = Path(build_config.root_dir)
SHADER_TEST_DIR = ROOT_DIR / build_config.test_dir / "shader"


def get_slangpy_python_dir() -> Path:
    """
    Return the directory to add to the Python path to import slangpy.

    :return: The Python package directory of the slangpy build dependency shared by every target.
    """
    dependency_build_dir = ROOT_DIR / "build" / build_config.dependency_build_dir
    return WBEBuildDependencySlangPy(dependency_build_dir, ROOT_DIR / "dependencies",
        build_config.dependency_build_info).get_python_package_dir()


def run_shader_tests(p_output: TextIO | None = None, p_pytest_args: list[str] | None = None) -> int:
    """
    Run the slangpy-based shader tests under tests/shader.

    slangpy is a build dependency of every test-generating target, so one such target must be built first.

    :param p_output: Stream receiving the test output, or None to inherit the process output.
    :param p_pytest_args: Extra arguments forwarded to pytest.
    :return: The pytest exit code.
    :raises RuntimeError: If slangpy has not been built.
    """
    slangpy_python_dir = get_slangpy_python_dir()
    if not (slangpy_python_dir / "slangpy").is_dir():
        raise RuntimeError("slangpy is not built. Build a test-generating target with build.py first.")
    environment = os.environ.copy()
    python_paths = [slangpy_python_dir.as_posix(), ROOT_DIR.as_posix()]
    if environment.get("PYTHONPATH"):
        python_paths.append(environment["PYTHONPATH"])
    environment["PYTHONPATH"] = os.pathsep.join(python_paths)
    test_command = [sys.executable, "-m", "pytest", "-p", "no:cacheprovider", SHADER_TEST_DIR.as_posix(),
        *(p_pytest_args or [])]
    if p_output is not None:
        p_output.flush()
    return subprocess.run(test_command, cwd=ROOT_DIR, env=environment, stdout=p_output,
        stderr=subprocess.STDOUT if p_output is not None else None, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(run_shader_tests(p_pytest_args=sys.argv[1:]))

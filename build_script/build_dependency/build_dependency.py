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

from abc import ABC, abstractmethod
import fcntl
from pathlib import Path
import shutil


class WBEBuildDependency(ABC):
    """A dependency that build.py compiles before running the resource pipeline and CMake."""

    @abstractmethod
    def get_name(self) -> str:
        """Return the name reported while compiling the dependency."""

    @abstractmethod
    def compile(self) -> None:
        """
        Configure and build the dependency.

        :raises RuntimeError: If the dependency fails to build.
        """

    @staticmethod
    def _discard_cmake_cache_on_generator_change(p_cmake_build_dir: Path, p_generator: str | None) -> None:
        """
        Discard the CMake cache of a build directory that was configured with a different generator.

        CMake refuses to reconfigure a build directory with another generator, so only CMakeCache.txt and CMakeFiles are
        removed; the rest of the build directory is kept.

        :param p_cmake_build_dir: The CMake build directory.
        :param p_generator: The generator the build directory is about to be configured with, or None for CMake's
            default, in which case the cache is kept.
        """
        cache_path = p_cmake_build_dir / "CMakeCache.txt"
        if p_generator is None or not cache_path.is_file():
            return
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                if line.removeprefix("CMAKE_GENERATOR:INTERNAL=") == p_generator:
                    return
                break
        print(f"WBEBuilder: Discarding the CMake cache in {p_cmake_build_dir}: its generator differs from {p_generator}.")
        cache_path.unlink()
        shutil.rmtree(p_cmake_build_dir / "CMakeFiles", ignore_errors=True)


class WBEBuildDependencies:
    """
    Ordered set of build dependencies compiled as one build task.

    Every target shares the same dependency build directory, so builds of different targets running at once take turns
    compiling the dependencies.
    """

    def __init__(self, p_build_dir: Path) -> None:
        """
        :param p_build_dir: The dependency build directory shared by every target.
        """
        self._build_dir = p_build_dir
        self._dependencies: list[WBEBuildDependency] = []

    def add(self, p_dependency: WBEBuildDependency) -> None:
        """Register a dependency; dependencies compile in registration order."""
        self._dependencies.append(p_dependency)

    def compile(self) -> None:
        """
        Compile every registered dependency.

        :raises RuntimeError: If any dependency fails to build.
        """
        self._build_dir.mkdir(parents=True, exist_ok=True)
        with open(self._build_dir / ".build.lock", "w", encoding="utf-8") as lock_file:
            fcntl.flock(lock_file, fcntl.LOCK_EX)
            for dependency in self._dependencies:
                print(f"WBEBuilder: Building dependency: {dependency.get_name()}...")
                dependency.compile()

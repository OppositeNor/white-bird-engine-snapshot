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
import shutil
import subprocess
import sys
from typing import Any

from build_script.build_dependency.build_dependency import WBEBuildDependency


class WBEBuildDependencySlangPy(WBEBuildDependency):
    """
    Build slangpy from dependencies/slangpy for the shader tests.

    slangpy's build writes its extension module, generated stubs, and a build-directory marker into the source package
    dependencies/slangpy/slangpy (all ignored by its .gitignore), so only one slangpy build directory may exist. The
    extension is installed into a copy of the Python package. The build does not use vcpkg: libpng is built from
    dependencies/libpng as a static library and passed to slangpy explicitly, while its other optional libraries (libjpeg,
    OpenEXR, asmjit) are taken from the system when found and are otherwise disabled.
    """

    def __init__(self, p_build_root_dir: Path, p_dependencies_dir: Path, p_build_info: dict[str, Any]) -> None:
        """
        :param p_build_root_dir: The dependency build directory shared by every target.
        :param p_dependencies_dir: The vendored dependency directory containing slangpy.
        :param p_build_info: The shared dependency build settings.
        """
        self._source_dir = p_dependencies_dir / "slangpy"
        self._libpng_source_dir = p_dependencies_dir / "libpng"
        self._build_dir = p_build_root_dir / "slangpy"
        self._build_info = p_build_info

    def get_name(self) -> str:
        return "slangpy"

    def get_python_package_dir(self) -> Path:
        """Return the directory to add to the Python path to import slangpy."""
        return self._build_dir / "python"

    def _compile_libpng(self) -> tuple[Path, Path]:
        """
        Build and install the vendored libpng as a position-independent static library for libsgl to link.

        libpng is always built in release so that its library name does not depend on the build type.

        :return: The installed static library and include directory.
        :raises RuntimeError: If libpng fails to build.
        """
        cmake_build_dir = self._build_dir / "libpng" / "cmake"
        install_dir = self._build_dir / "libpng" / "install"
        configure_command = [
            "cmake",
            "-S", self._libpng_source_dir.as_posix(),
            "-B", cmake_build_dir.as_posix(),
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_C_COMPILER={self._build_info['c-compiler']}",
            f"-DCMAKE_INSTALL_PREFIX={install_dir.as_posix()}",
            "-DCMAKE_INSTALL_LIBDIR=lib",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-DPNG_SHARED=OFF",
            "-DPNG_STATIC=ON",
            "-DPNG_TESTS=OFF",
            "-DPNG_TOOLS=OFF",
        ]
        if self._build_info.get("generator") is not None:
            configure_command.extend(["-G", self._build_info["generator"]])
        self._discard_cmake_cache_on_generator_change(cmake_build_dir, self._build_info.get("generator"))
        if subprocess.run(configure_command, check=False).returncode != 0:
            raise RuntimeError("Failed to configure libpng.")
        build_command = ["cmake", "--build", cmake_build_dir.as_posix(), "--config", "Release"]
        if os.cpu_count() is not None:
            build_command.extend(["-j", str(os.cpu_count())])
        if subprocess.run(build_command, check=False).returncode != 0:
            raise RuntimeError("Failed to build libpng.")
        install_command = ["cmake", "--install", cmake_build_dir.as_posix(), "--config", "Release"]
        if subprocess.run(install_command, check=False).returncode != 0:
            raise RuntimeError("Failed to install libpng.")
        return install_dir / "lib" / "libpng16.a", install_dir / "include"

    def compile(self) -> None:
        png_library, png_include_dir = self._compile_libpng()
        cmake_build_dir = self._build_dir / "cmake"
        package_dir = self.get_python_package_dir() / "slangpy"
        shutil.copytree(self._source_dir / "slangpy", package_dir, dirs_exist_ok=True,
            ignore=shutil.ignore_patterns("__pycache__", "*.so", "*.pyd"))
        configure_command = [
            "cmake",
            "-S", self._source_dir.as_posix(),
            "-B", cmake_build_dir.as_posix(),
            f"-DCMAKE_BUILD_TYPE={self._build_info['cmake-build-type']}",
            f"-DCMAKE_C_COMPILER={self._build_info['c-compiler']}",
            f"-DCMAKE_CXX_COMPILER={self._build_info['cpp-compiler']}",
            f"-DPython_ROOT_DIR:PATH={sys.prefix}",
            f"-DPython_EXECUTABLE:FILEPATH={sys.executable}",
            f"-DCMAKE_INSTALL_PREFIX={package_dir.as_posix()}",
            "-DCMAKE_INSTALL_LIBDIR=.",
            "-DCMAKE_INSTALL_BINDIR=.",
            "-DCMAKE_INSTALL_INCLUDEDIR=include",
            "-DCMAKE_INSTALL_DATAROOTDIR=.",
            "-DSGL_BUILD_EXAMPLES=OFF",
            "-DSGL_BUILD_TESTS=OFF",
            # Passed explicitly so that FindPNG neither searches the system nor keeps a library found by an earlier
            # configure.
            f"-DPNG_LIBRARY_RELEASE:FILEPATH={png_library.as_posix()}",
            f"-DPNG_PNG_INCLUDE_DIR:PATH={png_include_dir.as_posix()}",
            # Third-party warnings, such as clang's macro redefinitions from the conda Python headers, must not fail
            # the engine build.
            "-DSGL_WARNINGS_AS_ERRORS=OFF",
        ]
        if self._build_info.get("generator") is not None:
            configure_command.extend(["-G", self._build_info["generator"]])
        self._discard_cmake_cache_on_generator_change(cmake_build_dir, self._build_info.get("generator"))
        if subprocess.run(configure_command, check=False).returncode != 0:
            raise RuntimeError("Failed to configure slangpy.")
        build_type = self._build_info["cmake-build-type"]
        build_command = ["cmake", "--build", cmake_build_dir.as_posix(), "--config", build_type]
        if os.cpu_count() is not None:
            build_command.extend(["-j", str(os.cpu_count())])
        if subprocess.run(build_command, check=False).returncode != 0:
            raise RuntimeError("Failed to build slangpy.")
        install_command = ["cmake", "--install", cmake_build_dir.as_posix(), "--config", build_type]
        if subprocess.run(install_command, check=False).returncode != 0:
            raise RuntimeError("Failed to install slangpy.")

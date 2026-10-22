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
import os
from pathlib import Path
from build_script.utils import list_files

# enable_shader_debug defaults to False when omitted by a target.
# enable-tsan defaults to False when omitted by a target; True builds everything with ThreadSanitizer.
target_info = {
    "debug" : {
        "c-compiler" : "clang",
        "cpp-compiler" : "clang++",
        "export-directory" : "debug",
        "cmake-build-type" : "Debug",
        "enable_shader_debug" : True,
        "build-shared" : True,
        "generate-tests" : True,
    },
    "release" : {
        "c-compiler" : "clang",
        "cpp-compiler" : "clang++",
        "export-directory" : "release",
        "cmake-build-type" : "Release",
        "build-shared" : True,
        "generate-tests" : True,
    },
    "debug-ninja" : {
        "c-compiler" : "clang",
        "cpp-compiler" : "clang++",
        "export-directory" : "debug-ninja",
        "cmake-build-type" : "Debug",
        "enable_shader_debug" : True,
        "build-shared" : True,
        "generator" : "Ninja",
        "generate-tests" : True,
    },
    "release-ninja" : {
        "c-compiler" : "clang",
        "cpp-compiler" : "clang++",
        "export-directory" : "release-ninja",
        "cmake-build-type" : "Release",
        "build-shared" : True,
        "generator" : "Ninja",
        "generate-tests" : True,
    },
    "debug-tsan" : {
        "c-compiler" : "clang",
        "cpp-compiler" : "clang++",
        "export-directory" : "debug-tsan",
        "cmake-build-type" : "Debug",
        "build-shared" : True,
        "generate-tests" : True,
        "enable-tsan" : True,
    },
    "deploy" : {
        "c-compiler" : "clang",
        "cpp-compiler" : "clang++",
        "export-directory" : "deploy",
        "cmake-build-type" : "Deploy",
        "build-shared" : True,
        "generator" : "Ninja",
        "generate-tests" : False,
    },
    "debug-gcc" : {
        "c-compiler" : "gcc",
        "cpp-compiler" : "g++",
        "export-directory" : "debug-gcc",
        "build-shared" : True,
        "cmake-build-type" : "Debug",
        "enable_shader_debug" : True,
        "generate-tests" : True,
    },
    "release-gcc" : {
        "c-compiler" : "gcc",
        "cpp-compiler" : "g++",
        "export-directory" : "release-gcc",
        "cmake-build-type" : "Release",
        "build-shared" : True,
        "generate-tests" : True,
    },
    "debug-gcc-ninja" : {
        "c-compiler" : "gcc",
        "cpp-compiler" : "g++",
        "export-directory" : "debug-gcc-ninja",
        "cmake-build-type" : "Debug",
        "enable_shader_debug" : True,
        "build-shared" : True,
        "generator" : "Ninja",
        "generate-tests" : True,
    },
    "release-gcc-ninja" : {
        "c-compiler" : "gcc",
        "cpp-compiler" : "g++",
        "export-directory" : "release-gcc-ninja",
        "cmake-build-type" : "Release",
        "build-shared" : True,
        "generator" : "Ninja",
        "generate-tests" : True,
    },
    "deploy-gcc" : {
        "c-compiler" : "gcc",
        "cpp-compiler" : "g++",
        "export-directory" : "deploy-gcc",
        "cmake-build-type" : "Deploy",
        "build-shared" : True,
        "generator" : "Ninja",
        "generate-tests" : False,
    },
}

default_target = "deploy"

header_extensions = set([".hh"])
src_extensions = set([".cpp"])
source_extensions = src_extensions.union(header_extensions)
include_dir = "include"
source_dir = "src"
test_dir = "tests"
test_env_dir = "test_env"
resource_dir = "res"
# Build dependencies (build_script/build_dependency) are built once, with these settings, into build/dependencies for
# every target. slang-rhi in slangpy always compiles with -Werror, and GCC reports warnings in it that clang does not.
dependency_build_dir = "dependencies"
# Include directories of the vendored dependencies, relative to the dependencies directory. build.py hands them to CMake as
# WBE_DEPENDENCY_INCLUDE_DIRS and WBE_TEST_DEPENDENCY_INCLUDE_DIRS, and to the reflection metaparser; declare them only here.
dependency_include_dirs = [
    "entt/single_include",
    "glm",
    "json/single_include",
    "toml++/include",
    "stb",
    "boost-headers",
    "miniaudio",
    "thread-pool/include",
    "SDL2/include",
    "lua/src",
    "VulkanMemoryAllocator/include",
    "KTX-Software/lib/include",
]
# Added only when tests are built.
test_dependency_include_dirs = [
    "googletest/googletest/include",
    "google-benchmark/include",
    "imgui",
    "imgui/backends",
]
dependency_build_info = {
    "c-compiler" : "clang",
    "cpp-compiler" : "clang++",
    "cmake-build-type" : "Release",
    "generator" : "Ninja",
}

gen_info = {
    "static_labels" : [
        "WBE_CHANNEL",
        "WBE_RENDER_OBJECT_TYPE",
        "WBE_RENDER_TASK",
    ],
    "dynamic_labels" : [
        "WBE_CHANNEL",
        "WBE_RENDER_RESOURCE",
        "WBE_RENDER_TASK",
        "WBE_RESOURCE_UNIT",
    ],
    "static_serializables" : [
        "WBE_SERIALIZABLE_STATIC",
        "WBE_COMPONENT",
    ],
    "serializables" : [
        "WBE_SERIALIZABLE",
        "WBE_CONFIG_OPTION",
    ],
    "cla_configurations" : [
        "WBE_CLA_CONFIGURATION",
    ],
    # Serializables whose deserialization rejects keys that are not fields or field aliases of the class or its bases.
    "strict_deserializations" : [
        "WBE_STRICT_DESERIALIZATION",
    ],
    # Classes registered in RenderTaskRegistryVK; see include/function/render_vk/render_task_registry_vk.hh.
    "render_tasks" : [
        "WBE_RENDER_TASK",
    ],
    # Enums given an EnumReflection specialization; see include/core/reflection/enum_reflection.hh.
    "reflected_enums" : [
        "WBE_REFLECT_ENUM",
    ],
    # Classes and enums visible to scripts, which the Teal declarations (templates/teal_types.d.tl.jinja) declare. They
    # cross the C++/script boundary as DataValue, so a script class is also a static serializable and a script enum is also
    # a reflected enum.
    "reflect_script" : [
        "WBE_REFLECT_SCRIPT",
    ],
}

gen_info["static_serializables"].extend(gen_info["serializables"])
gen_info["static_serializables"].extend(gen_info["reflect_script"])
gen_info["reflected_enums"].extend(gen_info["reflect_script"])

# Setup directories
root_dir = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))

project_files = list_files(root_dir, ignore_dirs=["dependencies", "generated",
                                                  ".cache", ".git", ".github", "__pycache__",
                                                  "build", ".codex", ".claude", ".vscode", "test_env", "other"])
project_sources = [path for path in project_files if Path(path).suffix in source_extensions]
project_headers = [path for path in project_sources if Path(path).suffix in header_extensions]
project_files_exclude_tests = list_files(root_dir, ignore_dirs=["tests", "dependencies", "generated",
                                                                ".cache", ".git", ".github", "__pycache__",
                                                                "build"])
project_sources_exclude_tests = [path for path in project_files_exclude_tests if Path(path).suffix in source_extensions]
project_headers_exclude_tests = [path for path in project_sources_exclude_tests if Path(path).suffix in header_extensions]
gen_info_files = list_files(root_dir, ignore_dirs=[".cache", ".git",
                                                   ".github", "__pycache__", "build"])
gen_info_files = [gen_info for gen_info in gen_info_files if os.path.basename(gen_info) == "generate.json"]

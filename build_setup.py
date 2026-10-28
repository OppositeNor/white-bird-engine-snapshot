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

from argparse import ArgumentParser
import os
from pathlib import Path
import sys
import shutil
import build_config
from build_script.resource.acp.acp import WBEACP
from build_script.build_dependency.build_dependency import WBEBuildDependencies
from build_script.build_dependency.build_dependency_acp import WBEBuildDependencyACP
from build_script.build_dependency.build_dependency_slangpy import WBEBuildDependencySlangPy

# SETUP
print("WBEBuilder: Setting up...")
# Parse CLA
arg_parser = ArgumentParser(
    prog=sys.argv[0],
    description="Build script to build White Bird Engine."
)
arg_parser.add_argument("-t", "--target", help="Build target.", choices=list(build_config.target_info.keys()), default=build_config.default_target)
args = arg_parser.parse_args()

build_target = build_config.target_info[args.target]

# Sources dirs
root_dir = build_config.root_dir
include_dir = os.path.join(root_dir, build_config.include_dir)
source_dir = os.path.join(root_dir, build_config.source_dir)
per_target_include_dir = os.path.join(include_dir, "per_target", args.target)
test_dir = os.path.join(root_dir, build_config.test_dir)
test_env_dir = os.path.join(root_dir, build_config.test_env_dir)
build_root_dir = os.path.join(root_dir, "build")
dependency_build_dir = os.path.join(build_root_dir, build_config.dependency_build_dir)
build_dir = os.path.join(build_root_dir, build_target["export-directory"])
binary_dir = os.path.join(build_dir, "bin")
dependencies_dir = os.path.join(root_dir, "dependencies")
template_dir = os.path.join(root_dir, "templates")
teal_dir = os.path.join(dependencies_dir, "tl")
# Resource dirs
resource_dir = os.path.join(root_dir, build_config.resource_dir)
resource_output_dir = os.path.join(binary_dir, "res")
test_env_res_output_dir = os.path.join(test_env_dir, "res")
config_dir = os.path.join(resource_dir, "config")
config_output_dir = os.path.join(resource_output_dir, "config")
test_env_config_dir = os.path.join(config_dir, "test")
test_env_config_output_dir = os.path.join(test_env_res_output_dir, "config")
shaders_incl_dir = os.path.join(resource_dir, "assets/shaders")
res_chunks_dir = os.path.join(resource_dir, "res_chunks")
res_chunks_output_dir = os.path.join(resource_output_dir, "res_chunks")
assets_dir = os.path.join(resource_dir, "assets")
# Script resources' ids are their module paths under this directory.
script_modules_dir = os.path.join(assets_dir, "scripts", "teal")
# The generated Teal declarations of the reflected engine types (wbe.d.tl).
script_types_dir = os.path.join(build_dir, "teal")
assets_output_dir = os.path.join(resource_output_dir, "assets")
metadata_path = os.path.join(test_env_dir, "metadata.json")
cache_dir = os.path.join(build_dir, "build_cache")
metadata_cache_dir = os.path.join(cache_dir, "source_metadata")
model_compiler_cache_dir = os.path.join(cache_dir, "model_compiler")
licenses_output_dir = os.path.join(resource_output_dir, "licenses")

# Create directories
os.makedirs(cache_dir, exist_ok=True)
os.makedirs(metadata_cache_dir, exist_ok=True)
os.makedirs(model_compiler_cache_dir, exist_ok=True)
os.makedirs(resource_dir, exist_ok=True)
os.makedirs(licenses_output_dir, exist_ok=True)
shutil.copytree(config_dir, config_output_dir, dirs_exist_ok=True, ignore=shutil.ignore_patterns("test"))
if build_target["generate-tests"]:
    shutil.copytree(config_dir, test_env_config_output_dir, dirs_exist_ok=True, ignore=shutil.ignore_patterns("test"))
    shutil.copytree(test_env_config_dir, test_env_config_output_dir, dirs_exist_ok=True)

# Setup ACP
acp_dependencies_compiler = WBEBuildDependencyACP(
    Path(root_dir), Path(dependency_build_dir), Path(dependencies_dir), build_config.dependency_build_info)
acp = WBEACP(Path(resource_dir), Path(resource_output_dir), enabled_test=False)
test_env_acp = WBEACP(Path(resource_dir), Path(test_env_res_output_dir), enabled_test=True)
acp_compilers_configured = False

# Setup build dependencies
build_dependencies = WBEBuildDependencies(Path(dependency_build_dir))
build_dependencies.add(acp_dependencies_compiler)
if build_target["generate-tests"]:
    build_dependencies.add(WBEBuildDependencySlangPy(
        Path(dependency_build_dir), Path(dependencies_dir), build_config.dependency_build_info))


def setup_acp_compilers() -> None:
    global acp_compilers_configured
    if acp_compilers_configured:
        return
    python_package_path = acp_dependencies_compiler.get_python_package_dir().resolve().as_posix()
    if python_package_path not in sys.path:
        sys.path.insert(0, python_package_path)

    from build_script.resource.acp.acp_compiler_file import WBEACPCompilerFile
    from build_script.resource.acp.acp_compiler_mesh import WBEACPCompilerMesh
    from build_script.resource.acp.acp_compiler_model import WBEACPCompilerModel
    from build_script.resource.acp.acp_compiler_script import WBEACPCompilerScript
    from build_script.resource.acp.acp_compiler_shader import WBEACPCompilerShader
    from build_script.resource.acp.acp_compiler_texture import TextureCompilerPyKTX, WBEACPCompilerTexture
    from build_script.resource.acp.sdf_compiler_mesh_to_sdf import SDFCompilerMeshToSDF

    texture_compiler = TextureCompilerPyKTX(
        acp_dependencies_compiler.get_ktx_library_dir(), acp_dependencies_compiler.get_python_package_dir())
    sdf_compiler = SDFCompilerMeshToSDF(texture_compiler)
    acp.add_compiler(WBEACPCompilerShader(Path(shaders_incl_dir)))
    acp.add_compiler(WBEACPCompilerTexture(texture_compiler))
    acp.add_compiler(WBEACPCompilerModel(texture_compiler, sdf_compiler, Path(model_compiler_cache_dir)))
    acp.add_compiler(WBEACPCompilerMesh(sdf_compiler))
    acp.add_compiler(WBEACPCompilerFile(["audio", "binary", "font", "image", "text", "video"]))
    acp.add_compiler(WBEACPCompilerScript(
        Path(teal_dir), [Path(script_modules_dir), Path(script_types_dir)], Path(script_types_dir)))

    test_env_acp.add_compiler(WBEACPCompilerShader(Path(shaders_incl_dir)))
    test_env_acp.add_compiler(WBEACPCompilerTexture(texture_compiler))
    test_env_acp.add_compiler(WBEACPCompilerModel(texture_compiler, sdf_compiler, Path(model_compiler_cache_dir)))
    test_env_acp.add_compiler(WBEACPCompilerMesh(sdf_compiler))
    test_env_acp.add_compiler(WBEACPCompilerFile(["audio", "binary", "font", "image", "text", "video"]))
    test_env_acp.add_compiler(WBEACPCompilerScript(
        Path(teal_dir), [Path(script_modules_dir), Path(script_types_dir)], Path(script_types_dir)))
    acp_compilers_configured = True

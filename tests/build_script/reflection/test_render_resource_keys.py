# Copyright 2026 OppositeNor
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
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any
import unittest

from jinja2 import Environment, FileSystemLoader

from build_script.reflection.metadata_types import WBEClassMetadata, WBEFieldMetadata, WBEMetadata, WBEMethodMetadata
from build_script.reflection.teal_types import TEAL_FILTERS, teal_render_tasks
from build_script.resource.acp.acp_compiler_script import WBEACPCompilerScript

_ROOT = Path(__file__).resolve().parents[3]


def _field(p_name: str, p_type: str, p_parameters: list[str] | None = None) -> WBEFieldMetadata:
    return WBEFieldMetadata(name=p_name, type=p_type,
                            attribute=["WBE_REFLECT"] + ([] if p_parameters is None else ["WBE_RECIPE_RESOURCE"]),
                            attribute_parameters={} if p_parameters is None else {"WBE_RECIPE_RESOURCE": p_parameters})


def _params(p_name: str, p_fields: list[WBEFieldMetadata]) -> WBEClassMetadata:
    return WBEClassMetadata(name=p_name, attribute=["WBE_REFLECT_SCRIPT"], fields=p_fields)


def _metadata(p_fields: list[WBEFieldMetadata]) -> WBEMetadata:
    return WBEMetadata(classes=[
        WBEClassMetadata(name="MeshResourcesVK", attribute=["WBE_RENDER_OBJECT"],
                         fields=[_field("private_cpp_state", "uint32_t")]),
        WBEClassMetadata(name="ROGraphicsPipelineVK", attribute=["WBE_RENDER_OBJECT"]),
        WBEClassMetadata(name="ROComputePipelineVK", attribute=["WBE_RENDER_OBJECT"]),
        _params("RTParamsDrawVK", p_fields),
        WBEClassMetadata(name="RTDrawVK", attribute=["WBE_RENDER_TASK"],
                         attribute_parameters={"WBE_RENDER_TASK": ["draw"]}, aliases=["wire_draw"],
                         methods=[WBEMethodMetadata(method_name="create", is_static=True,
                                                    type=["const RenderContextVK *", "const RTParamsDrawVK &",
                                                          "const RendGraphContextVK &"])])])


def _describe(p_metadata: WBEMetadata) -> dict[str, Any]:
    return teal_render_tasks(p_metadata, ["WBE_RENDER_TASK"], ["WBE_REFLECT_SCRIPT"])


def _render(p_metadata: WBEMetadata, p_template: str) -> str:
    environment = Environment(loader=FileSystemLoader(_ROOT / "templates"), trim_blocks=True, lstrip_blocks=True)
    environment.filters.update(TEAL_FILTERS)
    return environment.get_template(p_template).render(metadata=p_metadata, render_tasks=["WBE_RENDER_TASK"],
                                                        reflect_script=["WBE_REFLECT_SCRIPT"], _prefix="Test.")


def _write_modules(p_directory: Path, p_metadata: WBEMetadata) -> WBEACPCompilerScript:
    core_types = ["RendRecipeResourceVK", "RendImageViewParamsVK", "RendRecipeTriggerVK", "RendRecipeToggleVK",
                  "RendGraphRecipeVK", "RendResPrimaryUsageVK", "ImageFormatVK", "RendExtent2DParamsVK",
                  "ImageUsageFlagVK", "RendSamplerParamsVK", "RTParamsComputeDispatchVK"]
    metadata = WBEMetadata(classes=p_metadata.classes + [_params(name, []) for name in core_types])
    (p_directory / "render_graph_utils").mkdir()
    (p_directory / "wbe.d.tl").write_text(_render(metadata, "teal_types.d.tl.jinja"))
    (p_directory / "render_graph_utils/render_graph.tl").write_text(_render(metadata, "teal_render_graph.tl.jinja"))
    (p_directory / "render_graph_utils/render_graph_tasks.tl").write_text(_render(metadata, "teal_render_graph_tasks.tl.jinja"))
    return WBEACPCompilerScript(_ROOT / "dependencies/tl", [p_directory], p_directory)


class DatabaseRecipeResourcesTest(unittest.TestCase):
    def test_render_objects_do_not_expose_live_state_or_add_script_marker_types(self) -> None:
        metadata = _metadata([])
        declarations = _render(metadata, "teal_types.d.tl.jinja")
        self.assertNotIn("interface MeshResourcesVK", declarations)
        self.assertNotIn("interface ROGraphicsPipelineVK", declarations)
        self.assertNotIn("private_cpp_state", declarations)

    def test_database_fields_are_handle_only_and_recurse_through_params_and_containers(self) -> None:
        metadata = _metadata([
            _field("mesh", "std::optional<std::string>", ["database", "MeshResourcesVK"]),
            _field("pipelines", "std::vector<std::string>", ["database", "WhiteBirdEngine::ROGraphicsPipelineVK"]),
            _field("resources", "DataValue", ["database", "MeshResourcesVK"]),
            _field("nested", "std::vector<NestedParams>"),
        ])
        metadata.classes.append(_params("NestedParams", [
            _field("pipeline", "std::string", ["database", "ROComputePipelineVK"]),
            _field("buffer", "std::string", ["graph"]),
        ]))
        api = _describe(metadata)
        mesh = {"kind": "resource", "scope": "database", "expected_type": "WhiteBirdEngine::MeshResourcesVK"}
        fields = api["constructors"][0]["fields"]
        self.assertEqual(fields[0], {"name": "mesh", **mesh})
        self.assertEqual(fields[1], {"name": "pipelines", "kind": "list", "element": {
            "kind": "resource", "scope": "database", "expected_type": "WhiteBirdEngine::ROGraphicsPipelineVK"}})
        self.assertEqual(fields[2], {"name": "resources", "kind": "map", "element": mesh})
        self.assertEqual(fields[3]["element"]["fields"][0]["expected_type"], "WhiteBirdEngine::ROComputePipelineVK")
        interfaces = {interface["name"]: interface["fields"] for interface in api["interfaces"]}
        self.assertEqual(interfaces["RTParamsDrawVK"][0]["type"], "Resource")
        self.assertEqual(interfaces["RTParamsDrawVK"][1]["type"], "{Resource}")
        self.assertEqual(interfaces["RTParamsDrawVK"][2]["type"], "{string:Resource}")
        self.assertEqual(interfaces["NestedParams"][1]["type"], "Resource | string")

    def test_invalid_annotations_and_non_render_object_targets_fail_generation(self) -> None:
        for parameters in [["database"], ["database", "MeshResourcesVK", "extra"], ["graph", "MeshResourcesVK"]]:
            with self.subTest(parameters=parameters), self.assertRaisesRegex(ValueError, "RenderObjectType"):
                _describe(_metadata([_field("mesh", "std::string", parameters)]))
        for target in ["MissingMeshVK", "RTParamsDrawVK", '"MeshResourcesVK"']:
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, "WBE_RENDER_OBJECT"):
                _describe(_metadata([_field("mesh", "std::string", ["database", target])]))
        with self.assertRaisesRegex(ValueError, "std::string ids"):
            _describe(_metadata([_field("mesh", "uint32_t", ["database", "MeshResourcesVK"])]))

    def test_generated_params_accept_common_resources_and_reject_string_keys(self) -> None:
        metadata = _metadata([_field("mesh", "std::string", ["database", "MeshResourcesVK"])])
        with TemporaryDirectory() as temporary:
            generated = Path(temporary)
            compiler = _write_modules(generated, metadata)
            tasks = compiler._compile_teal(generated / "render_graph_utils/render_graph_tasks.tl")
            self.assertIn('expected_type = "WhiteBirdEngine::MeshResourcesVK"', tasks)
            self.assertNotIn("render_resources", tasks)
            consumer = generated / "consumer.tl"
            prefix = '''local type Graph = require("render_graph_utils.render_graph")
local mesh: Graph.Resource = { id = "static_mesh", kind = "database", resource_type = "WhiteBirdEngine::MeshResourcesVK" }
local pipeline: Graph.Resource = { id = "pipeline", kind = "database", resource_type = "WhiteBirdEngine::ROGraphicsPipelineVK" }
'''
            # The common Resource type keeps the public authoring API uniform. The expected type is checked at runtime.
            for value in ["mesh", "pipeline"]:
                consumer.write_text(prefix + f"local params: Graph.RTParamsDrawVK = {{ mesh = {value} }}\nreturn params\n")
                compiler._compile_teal(consumer)
            for value in ['"static_mesh"', "5"]:
                consumer.write_text(prefix + f"local params: Graph.RTParamsDrawVK = {{ mesh = {value} }}\nreturn params\n")
                with self.subTest(value=value), redirect_stdout(StringIO()), self.assertRaises(RuntimeError):
                    compiler._compile_teal(consumer)

    def test_generated_task_module_packages_without_a_separate_resource_module(self) -> None:
        metadata = _metadata([_field("mesh", "std::string", ["database", "MeshResourcesVK"])])
        with TemporaryDirectory() as temporary:
            directory = Path(temporary)
            generated = directory / "generated"
            generated.mkdir()
            compiler = _write_modules(generated, metadata)
            output = directory / "output"
            resources = compiler.compile({"id": "render_graph_utils.render_graph_tasks", "source_type": "script",
                                          "source_file": "render_graph_utils/render_graph_tasks.tl",
                                          "path": "assets/scripts/teal/render_graph_utils/render_graph_tasks.lua"},
                                         Path("lua_scripts.toml"), directory / "res", output)
            self.assertEqual(resources[0]["path"], "assets/scripts/teal/render_graph_utils/render_graph_tasks.lua")
            self.assertTrue((output / "assets/scripts/teal/render_graph_utils/render_graph_tasks.lua").is_file())


if __name__ == "__main__":
    unittest.main()

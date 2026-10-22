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
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import clang.cindex

from build_script.reflection.metadata_types import WBEFileMetadata, WBEMetadata
from build_script.reflection.metaparser import WBEMetaparser


_HEADER = r'''
#define WBE_ATTR(...) __attribute__((annotate(#__VA_ARGS__)))
#define WBE_META(...) WBE_ATTR(WBE, __VA_ARGS__)
namespace WhiteBirdEngine {
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_RENDER_TASK(compute),
                CUSTOM(factory(1, {2, 3}), "a, [b]"), WBE_SERIALIZATION_ALIASES(LegacyExample)) Example {
    WBE_META(WBE_REFLECT, RESOURCE_REF(buffer), WBE_SERIALIZATION_ALIASES(old_value))
    int value;
    WBE_META(WBE_REFLECT, METHOD_METADATA(call({4, 5}, "comma,quote")),
             WBE_SERIALIZATION_ALIASES(old_create))
    static int create(int input);
};
enum class WBE_META(WBE_REFLECT_ENUM, ENUM_METADATA(scope), WBE_SERIALIZATION_ALIASES(LegacyChoice)) Choice {
    FIRST WBE_META(WBE_SERIALIZATION_ALIASES(first), VALUE_METADATA(1, nested(2, 3))) = 4,
    SECOND = 8
};
constexpr int WBE_META(LABEL_METADATA(event), WBE_SERIALIZATION_ALIASES(old_label)) label = 1;
struct WBE_META() Empty {};
}
'''


class _MetadataSink:
    def __init__(self) -> None:
        self.metadata = WBEMetadata()

    def register_metadata(self, metadata: WBEMetadata) -> None:
        self.metadata = metadata


class MetaparserAttributesTest(unittest.TestCase):
    def test_extracts_parameters_on_every_attributed_node_and_roundtrips_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            header = directory / "attributes.hh"
            header.write_text(_HEADER)
            sink = _MetadataSink()
            parser = WBEMetaparser(sink, ["-x", "c++", "-std=c++20"], temporary_directory, [str(header)])

            # Check the fixture itself before asserting on the metadata extracted from clang.
            translation_unit = clang.cindex.Index.create().parse(str(header), args=parser.clang_args)
            errors = [diagnostic.spelling for diagnostic in translation_unit.diagnostics
                      if diagnostic.severity >= clang.cindex.Diagnostic.Error]
            self.assertEqual(errors, [])
            del translation_unit
            with redirect_stdout(io.StringIO()):
                parser.parse(str(header))
                parser.export()

            example, empty = sink.metadata.classes
            self.assertIn("WBE_RENDER_TASK", example.attribute)
            self.assertEqual(example.attribute_parameters["WBE_RENDER_TASK"], ["compute"])
            self.assertEqual(example.attribute_parameters["CUSTOM"], ['factory(1, {2, 3})', '"a, [b]"'])
            self.assertEqual(example.aliases, ["LegacyExample"])
            self.assertEqual(example.fields[0].attribute_parameters["RESOURCE_REF"], ["buffer"])
            self.assertEqual(example.fields[0].aliases, ["old_value"])
            self.assertEqual(example.methods[0].attribute_parameters["METHOD_METADATA"], ['call({4, 5}, "comma,quote")'])
            self.assertEqual(example.methods[0].aliases, ["old_create"])
            self.assertEqual(empty.attribute, ["WBE"])
            self.assertEqual(empty.attribute_parameters, {})

            choice = sink.metadata.enums[0]
            self.assertEqual(choice.attribute_parameters["ENUM_METADATA"], ["scope"])
            self.assertEqual(choice.aliases, ["LegacyChoice"])
            self.assertEqual(choice.values[0].attribute_parameters["VALUE_METADATA"], ["1", "nested(2, 3)"])
            self.assertEqual(choice.values[0].aliases, ["first"])
            self.assertEqual(choice.values[1].attribute_parameters, {})
            self.assertEqual(sink.metadata.labels[0].attribute_parameters["LABEL_METADATA"], ["event"])
            self.assertEqual(sink.metadata.labels[0].aliases, ["old_label"])

            cache_path = next(directory.glob("*.json"))
            cached_file = WBEFileMetadata.model_validate_json(cache_path.read_text())
            self.assertEqual(WBEFileMetadata.model_validate_json(cached_file.model_dump_json()), cached_file)
            cache_sink = _MetadataSink()
            cached_parser = WBEMetaparser(cache_sink, parser.clang_args, temporary_directory, [str(header)])
            with patch.object(cached_parser, "_register_from_clang", side_effect=AssertionError("Cache was not reused")):
                cached_parser.parse(str(header))
            cached_parser.export()
            self.assertEqual(cache_sink.metadata, sink.metadata)

    def test_parser_version_changes_invalidate_unchanged_source_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            header = Path(temporary_directory) / "attributes.hh"
            header.write_text(_HEADER)
            first = WBEMetaparser(_MetadataSink(), ["-x", "c++", "-std=c++20"], temporary_directory, [str(header)])
            with redirect_stdout(io.StringIO()):
                first.parse(str(header))
            with patch("build_script.reflection.metaparser.METAPARSER_VERSION", 0):
                changed_parser = WBEMetaparser(_MetadataSink(), first.clang_args, temporary_directory, [str(header)])
            with patch.object(changed_parser, "_register_from_clang", wraps=changed_parser._register_from_clang) as parse:
                with redirect_stdout(io.StringIO()):
                    changed_parser.parse(str(header))
                parse.assert_called_once()


if __name__ == "__main__":
    unittest.main()

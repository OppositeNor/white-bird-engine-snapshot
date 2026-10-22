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
import unittest

from build_script.reflection.attribute_parser import parse_metadata_attributes


class AttributeParserTest(unittest.TestCase):
    def test_separates_names_from_parameters(self) -> None:
        attributes, parameters, aliases = parse_metadata_attributes(
            "WBE, WBE_REFLECT, WBE_RENDER_TASK(compute), CUSTOM_EMPTY()")
        self.assertEqual(attributes, ["WBE", "WBE_REFLECT", "WBE_RENDER_TASK", "CUSTOM_EMPTY"])
        self.assertEqual(parameters, {"WBE_RENDER_TASK": ["compute"], "CUSTOM_EMPTY": []})
        self.assertEqual(aliases, [])

    def test_keeps_nested_and_quoted_commas_in_their_parameter(self) -> None:
        expressions = [
            "factory(1, pair(2, 3), {4, 5})",
            "array[index(1, 2)]",
            r'"comma, brackets [] () {} and escaped \" quote"',
            "'['",
        ]
        attributes, parameters, _ = parse_metadata_attributes("CUSTOM(" + ", ".join(expressions) + "), MARKER")
        self.assertEqual(attributes, ["CUSTOM", "MARKER"])
        self.assertEqual(parameters, {"CUSTOM": expressions})

    def test_serialization_aliases_are_its_parameters(self) -> None:
        attributes, parameters, aliases = parse_metadata_attributes(
            "WBE_SERIALIZATION_ALIASES(legacy_name, OtherName, _third_name, 2d_array)")
        self.assertEqual(attributes, ["WBE_SERIALIZATION_ALIASES"])
        self.assertEqual(parameters["WBE_SERIALIZATION_ALIASES"], ["legacy_name", "OtherName", "_third_name", "2d_array"])
        self.assertEqual(aliases, ["legacy_name", "OtherName", "_third_name", "2d_array"])

    def test_accepts_empty_lists(self) -> None:
        self.assertEqual(parse_metadata_attributes("WBE, "), (["WBE"], {}, []))
        self.assertEqual(parse_metadata_attributes(""), ([], {}, []))
        self.assertEqual(parse_metadata_attributes("WBE_SERIALIZATION_ALIASES()"),
                         (["WBE_SERIALIZATION_ALIASES"], {"WBE_SERIALIZATION_ALIASES": []}, []))

    def test_rejects_assignment_syntax(self) -> None:
        for spelling in ["WBE_ALIASES = [old_name]", "WBE_SERIALIZATION_ALIASES = [old_name]", "CUSTOM = [value]"]:
            with self.subTest(spelling=spelling), self.assertRaises(ValueError):
                parse_metadata_attributes(spelling)

    def test_rejects_malformed_lists_and_duplicate_attributes(self) -> None:
        for spelling in ["CUSTOM(a), CUSTOM(b)", "CUSTOM, CUSTOM(a)", "CUSTOM(a), CUSTOM",
                         "CUSTOM(a,)", "CUSTOM(,a)", "CUSTOM(a,,b)", ",CUSTOM", "CUSTOM,,OTHER",
                         "CUSTOM([a,b)", "CUSTOM(a", "CUSTOM(a))", 'CUSTOM("unterminated)',
                         "CUSTOM(a)(b)", "CUSTOM(a) extra", "123BAD"]:
            with self.subTest(spelling=spelling), self.assertRaises(ValueError):
                parse_metadata_attributes(spelling)

    def test_rejects_serialization_aliases_that_are_not_names(self) -> None:
        for spelling in ['WBE_SERIALIZATION_ALIASES("quoted")', "WBE_SERIALIZATION_ALIASES(qualified::name)",
                         "WBE_SERIALIZATION_ALIASES(call())", "WBE_SERIALIZATION_ALIASES(two words)",
                         "WBE_SERIALIZATION_ALIASES"]:
            with self.subTest(spelling=spelling), self.assertRaisesRegex(ValueError, "aliases"):
                parse_metadata_attributes(spelling)


if __name__ == "__main__":
    unittest.main()

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
from typing import TYPE_CHECKING
from build_script.utils import hash_file, hash_str_sha256
from build_script.reflection.attribute_parser import parse_metadata_attributes
import clang.cindex
import json
from build_script.reflection.metadata_types import WBEClassMetadata, WBEEnumMetadata, WBEEnumValueMetadata, WBEFieldMetadata, WBEFileMetadata, WBELabelMetadata, WBEMetadata, WBEMethodMetadata

if TYPE_CHECKING:
    from build_script.reflection.reflect import WBEReflector

WBE_REFLECT = "WBE_REFLECT"

# Bump whenever the extracted metadata changes, so cached metadata of unchanged headers is reparsed. Changes to the clang
# arguments invalidate the cache on their own.
METAPARSER_VERSION = 5

class WBEMetaparser:
    """Metaparser

    Attributes: 
        reflector: The reflector.
        clang_args: The arguments for clang.
        cache_dir: The cache directory.
    """
    def __init__(self, reflector: "WBEReflector", clang_args: list[str], cache_dir: str,
                 sources: list[str]) -> None:
        """Constructor.

        Args:
            reflector: The reflector for the metaparser.
            clang_args: Clang arguments for parsing.
            cache_dir: The cache directory.
            sources: The list of source files being parsed.
        """
        self.reflector = reflector
        self.clang_args = clang_args
        self.cache_dir = cache_dir
        self.sources = sources
        self._metadata: dict[str, WBEFileMetadata] = {}
        self._parser_signature = hash_str_sha256("\0".join([str(METAPARSER_VERSION), *clang_args]))

    def parse(self, cpp_file_path : str) -> None:
        """Parse the C++ file.

        Args:
            cpp_file_path: The path to the C++ file.
        """
        file_cache_path = os.path.join(self.cache_dir, f"{hash_str_sha256(cpp_file_path)}.json")
        self._get_metadata(cpp_file_path, file_cache_path)

    def export(self) -> None:
        """Export"""
        result_metadata = WBEMetadata()
        sorted_metadata = self._sorted_metadata_by_dependency()
        for metadata in sorted_metadata:
            result_metadata.labels.extend(metadata.labels)
            result_metadata.classes.extend(metadata.classes)
            result_metadata.enums.extend(metadata.enums)
        self.reflector.register_metadata(result_metadata)

    def _get_metadata(self, cpp_file_path: str, cache_path: str) -> WBEFileMetadata:
        preloaded = self._metadata.get(cpp_file_path)
        if preloaded is not None:
            return preloaded
        if os.path.exists(cache_path):
            result = self._register_from_cache(cpp_file_path, cache_path)
        else:
            result = self._register_from_clang(cpp_file_path, cache_path)
        return result

    def _register_from_cache(self, cpp_file_path: str, cache_path: str) -> WBEFileMetadata:
        try:
            with open(cache_path) as f:
                data = json.load(f)
            metadata = WBEFileMetadata(**data)
            file_content_hashcode = hash_file(cpp_file_path)
            if metadata.hashcode != file_content_hashcode or metadata.parser_signature != self._parser_signature:
                return self._register_from_clang(cpp_file_path, cache_path)
            self._metadata[cpp_file_path] = metadata
            return metadata
        except Exception as e:
            print("Exception thrown:", e)
            print("Retrying...")
            return self._register_from_clang(cpp_file_path, cache_path)

    def _sorted_metadata_by_dependency(self) -> list[WBEFileMetadata]:
        result: list[list] = []
        metadata_with_depth: dict[str, list] = {key : [0, value] for key, value in self._metadata.items()}
        for _, metadata in metadata_with_depth.items():
            deps = [metadata_with_depth.get(dep_metadata) for dep_metadata in metadata[1].deps]
            for dep in deps:
                if dep is not None:
                    dep[0] += 1
            result.append(metadata)
        result.sort(key=lambda v: v[0], reverse=True)
        return [item[1] for item in result]

    def _register_from_clang(self, cpp_file_path: str, cache_path: str) -> WBEFileMetadata:
        print(f"WBEMetaparser: parsing {cpp_file_path}")
        index = clang.cindex.Index.create()
        tu = index.parse(
            cpp_file_path,
            args=self.clang_args,
            options=clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
        )
        metadata = WBEFileMetadata()
        metadata.deps = self._get_include_deps(tu)
        metadata.file_path = cpp_file_path
        self._register_metadata(tu, metadata, cpp_file_path)
        # A translation unit of an engine header holds the whole include closure; release it before the next parse.
        del tu
        del index
        metadata.hashcode = hash_file(cpp_file_path)
        metadata.parser_signature = self._parser_signature
        self._metadata[cpp_file_path] = metadata
        with open(cache_path, "w") as f:
            json.dump(metadata.model_dump(), f, indent=4)
        return metadata

    def _register_metadata(self, tu: clang.cindex.TranslationUnit, metadata: WBEFileMetadata, in_file: str) -> None:
        if tu.cursor is None:
            return
        self._visit_attributes(metadata, tu.cursor, in_file)

    def _visit_attributes(self, metadata: WBEFileMetadata, cursor: clang.cindex.Cursor, in_file: str) -> None:
        if cursor.kind == clang.cindex.CursorKind.VAR_DECL:
            self._handle_visit_var_decl(metadata, cursor)
        elif cursor.kind == clang.cindex.CursorKind.CLASS_DECL or cursor.kind == clang.cindex.CursorKind.STRUCT_DECL:
            self._handle_visit_class_decl(metadata, cursor, in_file)
        elif cursor.kind == clang.cindex.CursorKind.ENUM_DECL:
            self._handle_visit_enum_decl(metadata, cursor, in_file)

        for child in cursor.get_children():
            # Every header registers only what it declares itself; included headers register their own declarations
            # when they are parsed, so declarations from other files are skipped together with their children.
            if child.kind != clang.cindex.CursorKind.ANNOTATE_ATTR and WBEMetaparser._is_declared_in(child, in_file):
                self._visit_attributes(metadata, child, in_file)

    @staticmethod
    def _is_declared_in(cursor: clang.cindex.Cursor, in_file: str) -> bool:
        """Whether a cursor is located in the given file."""
        location_file = cursor.location.file
        return location_file is not None and os.path.abspath(location_file.name) == os.path.abspath(in_file)

    @staticmethod
    def _is_in_namelist(name: str, namelist: list) -> bool:
        return any(name == item.name for item in namelist)

    def _handle_visit_var_decl(self, metadata: WBEFileMetadata, cursor: clang.cindex.Cursor) -> None:
        for attr in cursor.get_children():
            if attr.kind == clang.cindex.CursorKind.ANNOTATE_ATTR:
                if not WBEMetaparser._is_in_namelist(cursor.spelling, metadata.labels):
                    attributes, attribute_parameters, aliases = self._get_attributes(attr.spelling)
                    label_metadata = WBELabelMetadata(name=cursor.spelling, attribute=attributes,
                                                       attribute_parameters=attribute_parameters, aliases=aliases)
                    metadata.labels.append(label_metadata)

    def _handle_visit_class_decl(self, metadata: WBEFileMetadata, cursor: clang.cindex.Cursor, in_file: str) -> None:
        for attr in cursor.get_children():
            if attr.kind == clang.cindex.CursorKind.ANNOTATE_ATTR:
                if not WBEMetaparser._is_in_namelist(cursor.spelling, metadata.classes):
                    attributes, attribute_parameters, aliases = self._get_attributes(attr.spelling)
                    class_metadata = self._get_class_metadata(cursor, attributes, attribute_parameters, aliases, in_file)
                    metadata.classes.append(class_metadata)

    def _handle_visit_enum_decl(self, metadata: WBEFileMetadata, cursor: clang.cindex.Cursor, in_file: str) -> None:
        """Register an annotated enum, including the metadata and serialization aliases of its enumerators."""
        for attr in cursor.get_children():
            if attr.kind != clang.cindex.CursorKind.ANNOTATE_ATTR:
                continue
            qualified_name = cursor.type.get_canonical().spelling
            if any(existing.qualified_name == qualified_name for existing in metadata.enums):
                return
            attributes, attribute_parameters, aliases = self._get_attributes(attr.spelling)
            enum_metadata = WBEEnumMetadata(name=cursor.spelling,
                                            qualified_name=qualified_name,
                                            in_header=in_file,
                                            attribute=attributes,
                                            attribute_parameters=attribute_parameters,
                                            aliases=aliases,
                                            underlying_type=cursor.enum_type.get_canonical().spelling,
                                            is_scoped=cursor.is_scoped_enum())
            for enumerator in cursor.get_children():
                if enumerator.kind != clang.cindex.CursorKind.ENUM_CONSTANT_DECL:
                    continue
                value_attributes: list[str] = []
                value_attribute_parameters: dict[str, list[str]] = {}
                value_aliases: list[str] = []
                for enumerator_attr in enumerator.get_children():
                    if enumerator_attr.kind == clang.cindex.CursorKind.ANNOTATE_ATTR:
                        attributes, attribute_parameters, aliases = self._get_attributes(enumerator_attr.spelling)
                        duplicate_parameters = value_attribute_parameters.keys() & attribute_parameters.keys()
                        if duplicate_parameters:
                            raise ValueError(f"Duplicate enumerator metadata attributes: {sorted(duplicate_parameters)}")
                        value_attributes.extend(attributes)
                        value_attribute_parameters.update(attribute_parameters)
                        value_aliases.extend(aliases)
                enum_metadata.values.append(WBEEnumValueMetadata(name=enumerator.spelling,
                                                                 value=enumerator.enum_value,
                                                                 attribute=value_attributes,
                                                                 attribute_parameters=value_attribute_parameters,
                                                                 aliases=value_aliases))
            metadata.enums.append(enum_metadata)
            return

    def _get_include_deps(self, tu: clang.cindex.TranslationUnit) -> list[str]:
        deps: set[str] = set()
        for inclusion in tu.get_includes():
            incl_path_abs = os.path.abspath(inclusion.include.name)
            if incl_path_abs in self.sources:
                deps.add(incl_path_abs)
        return list(deps)

    def _get_class_metadata(self, cursor: clang.cindex.Cursor, attributes: list[str],
                            attribute_parameters: dict[str, list[str]], aliases: list[str],
                            in_file: str) -> WBEClassMetadata:
        result = WBEClassMetadata()
        result.in_header = in_file
        result.name = cursor.spelling
        result.attribute = attributes
        result.attribute_parameters = attribute_parameters
        result.aliases = aliases
        result.is_struct = cursor.kind == clang.cindex.CursorKind.STRUCT_DECL

        for field in cursor.get_children():
            if field.kind == clang.cindex.CursorKind.CXX_BASE_SPECIFIER:
                result.extended_parents.append(field.spelling)
            if field.kind == clang.cindex.CursorKind.FIELD_DECL:
                for attr in field.get_children():
                    if attr.kind != clang.cindex.CursorKind.ANNOTATE_ATTR:
                        continue
                    attributes, attribute_parameters, aliases = self._get_attributes(attr.spelling)
                    if WBE_REFLECT in attributes:
                        result.fields.append(WBEFieldMetadata(attribute=attributes,
                                                              aliases=aliases,
                                                              attribute_parameters=attribute_parameters,
                                                              name=field.spelling, type=field.type.spelling))
            if field.kind == clang.cindex.CursorKind.CXX_METHOD:
                method_metadata = self._get_method_metadata(field)
                if method_metadata is not None:
                    result.methods.append(method_metadata)
        return result

    def _get_method_metadata(self, cursor: clang.cindex.Cursor) -> WBEMethodMetadata | None:
        """Get the metadata of a method annotated with WBE_META(WBE_REFLECT, ...), like reflected fields.

        Args:
            cursor: The CXX_METHOD cursor.

        Returns:
            The method metadata, or None when the method is not reflected.
        """
        for attr in cursor.get_children():
            if attr.kind != clang.cindex.CursorKind.ANNOTATE_ATTR:
                continue
            attributes, attribute_parameters, aliases = self._get_attributes(attr.spelling)
            if WBE_REFLECT not in attributes:
                continue
            arguments = list(cursor.get_arguments())
            return WBEMethodMetadata(attribute=attributes,
                                     aliases=aliases,
                                     attribute_parameters=attribute_parameters,
                                     method_name=cursor.spelling,
                                     name=[argument.spelling for argument in arguments],
                                     type=[argument.type.spelling for argument in arguments],
                                     ret_type=cursor.result_type.spelling,
                                     is_static=cursor.is_static_method(),
                                     is_const=cursor.is_const_method())
        return None

    def _get_attributes(self, attr_spelling: str) -> tuple[list[str], dict[str, list[str]], list[str]]:
        return parse_metadata_attributes(attr_spelling)



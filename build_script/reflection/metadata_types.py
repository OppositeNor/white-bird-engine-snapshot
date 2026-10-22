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
from typing import Any
from pydantic import BaseModel, Field

class WBEFieldMetadata(BaseModel):
    """Field metadata.

    Attributes: 
        attribute_parameters: Raw parameter expressions indexed by their attribute name.
        field_name: The name of the field.
        field_type: The type of the field.
    """
    attribute : list[str] = Field(default_factory=list)
    attribute_parameters : dict[str, list[str]] = Field(default_factory=dict)
    aliases : list[str] = Field(default_factory=list[str])
    name : str = ""
    type : str = ""

class WBEMethodMetadata(BaseModel):
    """Method metadata.

    Attributes: 
        attribute: The WBE_META attributes of the method.
        attribute_parameters: Raw parameter expressions indexed by their attribute name.
        aliases: The aliases declared with WBE_SERIALIZATION_ALIASES.
        method_name: The name of the method.
        name: The names of the arguments, in declaration order; an unnamed argument has an empty name.
        type: The types of the arguments, in declaration order, as clang spells them.
        ret_type: The return type, as clang spells it.
        is_static: Whether the method is static.
        is_const: Whether the method is const-qualified.
    """
    attribute : list[str] = Field(default_factory=list)
    attribute_parameters : dict[str, list[str]] = Field(default_factory=dict)
    aliases : list[str] = Field(default_factory=list[str])
    method_name : str = ""
    name : list[str] = Field(default_factory=list)
    type : list[str] = Field(default_factory=list)
    ret_type : str = ""
    is_static : bool = False
    is_const : bool = False

class WBEClassMetadata(BaseModel):
    """Class metadata.

    Attributes: 
        attribute_parameters: Raw parameter expressions indexed by their attribute name.
        is_struct: Is it declared as struct.
        class_name: The name of the class.
        fields: The fields of the class.
        methods: The methods of the class.
    """
    is_struct : bool = False
    in_header : str = ""
    name : str = ""
    extended_parents : list[Any] = Field(default_factory=list[Any])
    attribute : list[str] = Field(default_factory=list[str])
    attribute_parameters : dict[str, list[str]] = Field(default_factory=dict)
    aliases : list[str] = Field(default_factory=list[str])
    fields : list[WBEFieldMetadata] = Field(default_factory=list[WBEFieldMetadata])
    methods : list[WBEMethodMetadata] = Field(default_factory=list[WBEMethodMetadata])

class WBEEnumValueMetadata(BaseModel):
    """Enumerator metadata.

    Attributes:
        name: The name of the enumerator.
        value: The value of the enumerator.
        attribute: The WBE_META attributes of the enumerator.
        attribute_parameters: Raw parameter expressions indexed by their attribute name.
        aliases: The aliases declared with WBE_SERIALIZATION_ALIASES on the enumerator.
    """
    name : str = ""
    value : int = 0
    attribute : list[str] = Field(default_factory=list[str])
    attribute_parameters : dict[str, list[str]] = Field(default_factory=dict)
    aliases : list[str] = Field(default_factory=list[str])

class WBEEnumMetadata(BaseModel):
    """Enum metadata.

    Attributes:
        name: The unqualified name of the enum.
        qualified_name: The fully qualified type name of the enum, usable from any scope.
        in_header: The header that declares the enum.
        attribute: The WBE_META attributes of the enum.
        attribute_parameters: Raw parameter expressions indexed by their attribute name.
        aliases: The aliases declared with WBE_SERIALIZATION_ALIASES on the enum.
        underlying_type: The underlying integer type, as clang spells it.
        is_scoped: Whether the enum is an enum class.
        values: The enumerators in declaration order.
    """
    name : str = ""
    qualified_name : str = ""
    in_header : str = ""
    attribute : list[str] = Field(default_factory=list[str])
    attribute_parameters : dict[str, list[str]] = Field(default_factory=dict)
    aliases : list[str] = Field(default_factory=list[str])
    underlying_type : str = ""
    is_scoped : bool = False
    values : list[WBEEnumValueMetadata] = Field(default_factory=list[WBEEnumValueMetadata])

class WBELabelMetadata(BaseModel):
    """Label metadata.

    Attributes: 
        attribute: The attribute of the label.
        attribute_parameters: Raw parameter expressions indexed by their attribute name.
        label_name: The name of the label.
    """
    attribute : list[str] = Field(default_factory=list[str])
    attribute_parameters : dict[str, list[str]] = Field(default_factory=dict)
    aliases : list[str] = Field(default_factory=list[str])
    name : str = ""

class WBEFileMetadata(BaseModel):
    """Metadata in a single C++ file.

    Attributes: 
        file_path: The path of the file.
        hashcode: The hashcode of the file's content.
        parser_signature: The metaparser version and clang arguments that produced this metadata; caches with another
            signature are reparsed.
        deps: The dependencies of the file. Usually from include or import module.
        labels: The registered labels metadata.
        classes: The registered classes metadata.
        enums: The registered enums metadata.
    """
    file_path : str = ""
    hashcode : str = ""
    parser_signature : str = ""
    deps : list[str] = Field(default_factory=list)
    labels : list[WBELabelMetadata] = Field(default_factory=list)
    classes : list[WBEClassMetadata] = Field(default_factory=list)
    enums : list[WBEEnumMetadata] = Field(default_factory=list)

class WBEMetadata(BaseModel):
    """Metadata.

    Attributes:
        labels: The registrated lables.
        classes: The registrated classes.
        enums: The registrated enums.
    """
    labels : list[WBELabelMetadata] = Field(default_factory=list[WBELabelMetadata])
    classes : list[WBEClassMetadata] = Field(default_factory=list[WBEClassMetadata])
    enums : list[WBEEnumMetadata] = Field(default_factory=list[WBEEnumMetadata])

    def sort(self) -> None:
        """Sor the fields. This may gurantee the order it generates will always be the same,
        which is usefull not triggering the CMake and metaparser's compilation for the generated
        files.
        """
        self.labels = sorted(self.labels, key=lambda label: label.name)
        self.classes = sorted(self.classes, key=lambda cxx_class: cxx_class.name)
        self.enums = sorted(self.enums, key=lambda cxx_enum: cxx_enum.qualified_name)

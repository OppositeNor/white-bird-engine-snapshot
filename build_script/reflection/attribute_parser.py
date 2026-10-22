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
"""Parse the attribute list that WBE_META stringifies into an annotation.

An attribute is a bare name, such as `WBE_REFLECT`, or a name with parenthesized parameters, such as
`WBE_RENDER_TASK(compute)`. Parameters stay trimmed source text for the generator that consumes the attribute; commas
nested in parentheses, brackets, braces, or quoted literals do not separate them.
"""
import re

SERIALIZATION_ALIASES = "WBE_SERIALIZATION_ALIASES"

_IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# Aliases name serialized values, which may start with a digit, as the image type alias 2d does.
_ALIAS_PATTERN = re.compile(r"[A-Za-z0-9_]+")
_CLOSING_DELIMITERS = {"(": ")", "[": "]", "{": "}"}


def _split_top_level(p_spelling: str, p_allow_trailing_comma: bool) -> list[str]:
    """Split at the commas outside nested delimiters and quoted literals, and trim each part."""
    parts: list[str] = []
    closing: list[str] = []
    start = 0
    position = 0
    while position < len(p_spelling):
        character = p_spelling[position]
        if character in "\"'":
            position += 1
            while position < len(p_spelling) and p_spelling[position] != character:
                position += 2 if p_spelling[position] == "\\" else 1
            if position >= len(p_spelling):
                raise ValueError(f"Unterminated quoted literal in metadata: {p_spelling!r}")
        elif character in _CLOSING_DELIMITERS:
            closing.append(_CLOSING_DELIMITERS[character])
        elif character in ")]}":
            if not closing or closing.pop() != character:
                raise ValueError(f"Mismatched {character!r} in metadata: {p_spelling!r}")
        elif character == "," and not closing:
            parts.append(p_spelling[start:position].strip())
            start = position + 1
        position += 1
    if closing:
        raise ValueError(f"Unclosed delimiter in metadata: {p_spelling!r}")
    parts.append(p_spelling[start:].strip())
    if parts == [""]:
        return []
    # WBE_META(...) stringifies to "WBE, ...", so an empty WBE_META() leaves a trailing comma.
    if p_allow_trailing_comma and parts[-1] == "":
        parts.pop()
    if "" in parts:
        raise ValueError(f"Empty attribute or parameter in metadata: {p_spelling!r}")
    return parts


def parse_metadata_attributes(p_spelling: str) -> tuple[list[str], dict[str, list[str]], list[str]]:
    """Parse the attribute list of one annotation.

    Args:
        p_spelling: The annotation string, such as `WBE, WBE_REFLECT, WBE_SERIALIZATION_ALIASES(old_name)`.

    Returns:
        The attribute names in declaration order, the parameters of each parenthesized attribute by name, and the
        aliases WBE_SERIALIZATION_ALIASES declares.

    Raises:
        ValueError: If the list is malformed, names an attribute twice, or declares serialization aliases with characters
            other than letters, digits, and underscores.
    """
    attributes: list[str] = []
    attribute_parameters: dict[str, list[str]] = {}
    for attribute in _split_top_level(p_spelling, True):
        name_match = _IDENTIFIER_PATTERN.match(attribute)
        if name_match is None:
            raise ValueError(f"Expected an attribute name in metadata: {attribute!r}")
        name = name_match.group()
        if name in attributes:
            raise ValueError(f"Duplicate metadata attribute: {name}")
        attributes.append(name)
        parameters = attribute[name_match.end():].strip()
        if parameters:
            if not parameters.startswith("(") or not parameters.endswith(")"):
                raise ValueError(f"Expected {name} or {name}(<parameters>) in metadata: {attribute!r}")
            attribute_parameters[name] = _split_top_level(parameters[1:-1], False)
    if SERIALIZATION_ALIASES in attributes and SERIALIZATION_ALIASES not in attribute_parameters:
        raise ValueError(f"{SERIALIZATION_ALIASES} requires a parenthesized list of aliases.")
    aliases = attribute_parameters.get(SERIALIZATION_ALIASES, [])
    for alias in aliases:
        if _ALIAS_PATTERN.fullmatch(alias) is None:
            raise ValueError(f"{SERIALIZATION_ALIASES} takes aliases of letters, digits, and underscores, not {alias!r}.")
    return attributes, attribute_parameters, list(aliases)

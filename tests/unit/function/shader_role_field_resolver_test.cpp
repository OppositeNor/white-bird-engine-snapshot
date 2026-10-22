/* Copyright 2025 OppositeNor

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "function/render/shader_metadata.hh"
#include "function/render/shader_role_field_resolver.hh"
#include "utils/utils.hh"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

static ShaderMetadataUserAttribute make_role_test_attribute(std::string_view p_name, std::string_view p_role = {}) {
    ShaderMetadataUserAttribute result;
    result.name = p_name;
    if (!p_role.empty()) {
        ShaderMetadataUserAttributeArgument argument;
        argument.kind = "string";
        argument.string_value = p_role;
        result.arguments.push_back(std::move(argument));
    }
    return result;
}

static ShaderMetadataTypeField make_role_test_field(
    std::string_view p_name, std::string_view p_annotation_name, std::string_view p_role = {}) {
    ShaderMetadataTypeField result;
    result.name = p_name;
    result.user_attributes.push_back(make_role_test_attribute(p_annotation_name, p_role));
    return result;
}

TEST(WBEShaderRoleFieldResolverTest, ResolvesDefaultAndExplicitRolesWithRequestedTraversal) {
    std::vector<ShaderMetadataTypeField> fields;
    fields.push_back(make_role_test_field("position", "WBE_VERTEX_INPUT"));
    ShaderMetadataTypeField nested;
    nested.name = "vertex_data";
    nested.fields.push_back(make_role_test_field("coordinates", "WBE_VERTEX_INPUT", "uv"));
    fields.push_back(std::move(nested));

    ShaderRoleFieldResolver resolver;
    const auto direct = resolver.collect_fields(fields, "WBE_VERTEX_INPUT", ShaderRoleFieldResolver::Traversal::DIRECT);
    ASSERT_EQ(direct.size(), 1);
    EXPECT_EQ(direct[0].role.get_name(), "position");
    EXPECT_EQ(direct[0].role.get_hash(), dynam_hash("position"));
    EXPECT_EQ(direct[0].metadata, fields.data());

    const auto recursive = resolver.collect_fields(fields, "WBE_VERTEX_INPUT", ShaderRoleFieldResolver::Traversal::RECURSIVE);
    ASSERT_EQ(recursive.size(), 2);
    EXPECT_EQ(recursive[0].role.get_name(), "position");
    EXPECT_EQ(recursive[1].role.get_name(), "uv");
    EXPECT_EQ(recursive[1].metadata, fields[1].fields.data());
}

TEST(WBEShaderRoleFieldResolverTest, RejectsMalformedAndDuplicateAnnotations) {
    ShaderRoleFieldResolver resolver;
    ShaderMetadataTypeField repeated = make_role_test_field("position", "WBE_VERTEX_INPUT");
    repeated.user_attributes.push_back(make_role_test_attribute("WBE_VERTEX_INPUT"));
    EXPECT_THROW(resolver.resolve_field(repeated, "WBE_VERTEX_INPUT"), std::runtime_error);

    ShaderMetadataTypeField wrong_argument = make_role_test_field("position", "WBE_VERTEX_INPUT");
    ShaderMetadataUserAttributeArgument integer_argument;
    integer_argument.kind = "int";
    wrong_argument.user_attributes[0].arguments.push_back(std::move(integer_argument));
    EXPECT_THROW(resolver.resolve_field(wrong_argument, "WBE_VERTEX_INPUT"), std::runtime_error);

    ShaderMetadataTypeField empty_name = make_role_test_field("", "WBE_VERTEX_INPUT");
    EXPECT_THROW(resolver.resolve_field(empty_name, "WBE_VERTEX_INPUT"), std::runtime_error);
    EXPECT_THROW(resolver.resolve_field(repeated, ""), std::runtime_error);
}

TEST(WBEShaderRoleFieldResolverTest, RejectsDuplicateRolesAndDistinctNameHashCollisions) {
    ShaderRoleFieldResolver resolver;
    std::vector<ShaderMetadataTypeField> duplicates{
        make_role_test_field("first", "WBE_TEXTURE", "albedo"), make_role_test_field("second", "WBE_TEXTURE", "albedo")};
    EXPECT_THROW(resolver.collect_fields(duplicates, "WBE_TEXTURE", ShaderRoleFieldResolver::Traversal::DIRECT), std::runtime_error);

    constexpr std::string_view FIRST_COLLISION = "role_002876ae4c05a223";
    constexpr std::string_view SECOND_COLLISION = "role_2ffa43e2b7d24f56";
    static_assert(dynam_hash(FIRST_COLLISION) == dynam_hash(SECOND_COLLISION));
    std::vector<ShaderMetadataTypeField> collisions{make_role_test_field("first", "WBE_TEXTURE", FIRST_COLLISION),
        make_role_test_field("second", "WBE_TEXTURE", SECOND_COLLISION)};
    EXPECT_THROW(resolver.collect_fields(collisions, "WBE_TEXTURE", ShaderRoleFieldResolver::Traversal::DIRECT), std::runtime_error);
}

} // namespace WhiteBirdEngine

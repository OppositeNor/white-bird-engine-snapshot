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
#include "function/render/shader_role_field_resolver.hh"
#include "function/render/shader_metadata.hh"
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

static void collect_shader_role_fields(const ShaderRoleFieldResolver& p_resolver,
    const std::vector<ShaderMetadataTypeField>& p_fields,
    std::string_view p_annotation_name,
    ShaderRoleFieldResolver::Traversal p_traversal,
    std::vector<ShaderRoleFieldResolver::Field>& p_result) {
    for (const ShaderMetadataTypeField& field : p_fields) {
        std::optional<ShaderRoleFieldResolver::Field> resolved_field = p_resolver.resolve_field(field, p_annotation_name);
        if (resolved_field.has_value()) {
            for (const ShaderRoleFieldResolver::Field& existing : p_result) {
                if (existing.role.get_name() == resolved_field->role.get_name()) {
                    throw std::runtime_error(std::format("Failed to resolve shader role annotation '{}': role '{}' is declared more than once.",
                        p_annotation_name,
                        resolved_field->role.get_name()));
                }
                if (existing.role.get_hash() == resolved_field->role.get_hash()) {
                    throw std::runtime_error(std::format("Failed to resolve shader role annotation '{}': distinct roles '{}' and '{}' collide at hash {}.",
                        p_annotation_name,
                        existing.role.get_name(),
                        resolved_field->role.get_name(),
                        resolved_field->role.get_hash()));
                }
            }
            p_result.push_back(std::move(*resolved_field));
        }
        if (p_traversal == ShaderRoleFieldResolver::Traversal::RECURSIVE) {
            collect_shader_role_fields(p_resolver, field.fields, p_annotation_name, p_traversal, p_result);
        }
    }
}

std::optional<ShaderRoleFieldResolver::Field> ShaderRoleFieldResolver::resolve_field(
    const ShaderMetadataTypeField& p_field, std::string_view p_annotation_name) const {
    if (p_annotation_name.empty()) {
        throw std::runtime_error("Failed to resolve shader role annotation: annotation name is empty.");
    }
    const ShaderMetadataUserAttribute* matched_attribute = nullptr;
    for (const ShaderMetadataUserAttribute& attribute : p_field.user_attributes) {
        if (attribute.name != p_annotation_name) {
            continue;
        }
        if (matched_attribute != nullptr) {
            throw std::runtime_error(std::format("Failed to resolve shader role annotation '{}': field '{}' repeats the annotation.",
                p_annotation_name,
                p_field.name));
        }
        matched_attribute = &attribute;
    }
    if (matched_attribute == nullptr) {
        return std::nullopt;
    }
    if (matched_attribute->arguments.size() > 1 ||
        (!matched_attribute->arguments.empty() && matched_attribute->arguments.front().kind != "string")) {
        throw std::runtime_error(std::format("Failed to resolve shader role annotation '{}': field '{}' requires one optional string.",
            p_annotation_name,
            p_field.name));
    }
    std::string_view role_name = p_field.name;
    if (!matched_attribute->arguments.empty() && !matched_attribute->arguments.front().string_value.empty()) {
        role_name = matched_attribute->arguments.front().string_value;
    }
    Field result;
    result.role = ShaderRole(role_name);
    result.metadata = &p_field;
    return result;
}

std::vector<ShaderRoleFieldResolver::Field> ShaderRoleFieldResolver::collect_fields(
    const std::vector<ShaderMetadataTypeField>& p_fields, std::string_view p_annotation_name, Traversal p_traversal) const {
    std::vector<Field> result;
    collect_shader_role_fields(*this, p_fields, p_annotation_name, p_traversal, result);
    return result;
}

} // namespace WhiteBirdEngine

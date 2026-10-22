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
#include "function/render/shader_metadata_query.hh"
#include "function/render/shader_metadata.hh"
#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace WhiteBirdEngine {

const ShaderMetadataEntryPoint& ShaderMetadataQuery::resolve_entry_point(
    const ShaderMetadata& p_metadata, std::string_view p_stage, std::string_view p_entry_name) const {
    if (!p_entry_name.empty()) {
        return find_entry_point(p_metadata, p_stage, p_entry_name);
    }
    const ShaderMetadataEntryPoint* result = nullptr;
    for (const ShaderMetadataEntryPoint& entry_point : p_metadata.entry_points) {
        if (entry_point.stage != p_stage) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(std::format("Failed to resolve shader entry point for stage '{}': multiple reflected entries exist, so the pipeline must specify one.",
                p_stage));
        }
        result = &entry_point;
    }
    if (result == nullptr) {
        throw std::runtime_error(std::format("Failed to find shader entry point for stage '{}'.", p_stage));
    }
    return *result;
}

const ShaderMetadataEntryPoint& ShaderMetadataQuery::find_entry_point(
    const ShaderMetadata& p_metadata, std::string_view p_stage, std::string_view p_entry_name) const {
    if (p_stage.empty() || p_entry_name.empty()) {
        throw std::runtime_error("Failed to find shader entry point: stage and entry name must be non-empty.");
    }
    const ShaderMetadataEntryPoint* result = nullptr;
    for (const ShaderMetadataEntryPoint& entry_point : p_metadata.entry_points) {
        if (entry_point.stage != p_stage || entry_point.name != p_entry_name) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(
                std::format("Failed to find shader entry point '{}:{}': reflected entry is duplicated.", p_stage, p_entry_name));
        }
        result = &entry_point;
    }
    if (result == nullptr) {
        throw std::runtime_error(std::format("Failed to find shader entry point '{}:{}'", p_stage, p_entry_name));
    }
    return *result;
}

const ShaderMetadataParameter* ShaderMetadataQuery::find_parameter(const ShaderMetadata& p_metadata, std::string_view p_name) const {
    if (p_name.empty()) {
        throw std::runtime_error("Failed to find shader parameter: name is empty.");
    }
    const ShaderMetadataParameter* result = nullptr;
    for (const ShaderMetadataParameter& parameter : p_metadata.parameters) {
        if (parameter.name != p_name) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(std::format("Failed to find shader parameter '{}': reflected parameter is duplicated.", p_name));
        }
        result = &parameter;
    }
    return result;
}

const ShaderMetadataTypeField* ShaderMetadataQuery::find_field(
    const std::vector<ShaderMetadataTypeField>& p_fields, std::string_view p_name) const {
    if (p_name.empty()) {
        throw std::runtime_error("Failed to find shader field: name is empty.");
    }
    const ShaderMetadataTypeField* result = nullptr;
    for (const ShaderMetadataTypeField& field : p_fields) {
        if (field.name != p_name) {
            continue;
        }
        if (result != nullptr) {
            throw std::runtime_error(std::format("Failed to find shader field '{}': reflected field is duplicated.", p_name));
        }
        result = &field;
    }
    return result;
}

} // namespace WhiteBirdEngine

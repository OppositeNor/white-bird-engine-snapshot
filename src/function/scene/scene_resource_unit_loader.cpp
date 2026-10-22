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
#include "function/scene/scene_resource_unit_loader.hh"
#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "function/scene/scene_resource_unit.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <any>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {
namespace {

[[noreturn]] void throw_loader_not_implemented(std::string_view p_resource_type) {
    throw std::runtime_error(
        std::string("Resource unit loader is not implemented for scene resource type: ") + std::string(p_resource_type) + ".");
}

} // namespace

std::span<const std::string> ResourceUnitLoaderComponent::get_supported_resource_types() const {
    static const std::array<std::string, 1> supported_resource_types = {"component"};
    return supported_resource_types;
}

void ResourceUnitLoaderComponent::populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const {
    (void)p_res_chunk;
    (void)p_scheme_data;
    throw_loader_not_implemented("component");
}

std::any ResourceUnitLoaderComponent::populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const {
    (void)p_res_chunk;
    (void)p_scheme;
    throw_loader_not_implemented("component");
}

std::span<const std::string> ResourceUnitLoaderPrefab::get_supported_resource_types() const {
    static const std::array<std::string, 1> supported_resource_types = {"prefab"};
    return supported_resource_types;
}

void ResourceUnitLoaderPrefab::populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const {
    (void)p_res_chunk;
    (void)p_scheme_data;
    throw_loader_not_implemented("prefab");
}

std::any ResourceUnitLoaderPrefab::populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const {
    (void)p_res_chunk;
    (void)p_scheme;
    throw_loader_not_implemented("prefab");
}

std::span<const std::string> ResourceUnitLoaderWorld::get_supported_resource_types() const {
    static const std::array<std::string, 1> supported_resource_types = {"world"};
    return supported_resource_types;
}

void ResourceUnitLoaderWorld::populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const {
    RUSchemaWorld scheme;
    scheme.deserialize(p_scheme_data);
    populate_scheme_ptr(p_res_chunk, scheme);
}

std::any ResourceUnitLoaderWorld::populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const {
    const auto* typed_scheme = dynamic_cast<const RUSchemaWorld*>(&p_scheme);
    if (typed_scheme == nullptr) {
        throw std::runtime_error("Failed to populate world resource unit: expected world scheme but got type " + p_scheme.type + ".");
    }
    const std::string& static_mesh_render = typed_scheme->static_component.static_mesh_render;
    if (static_mesh_render.empty()) {
        throw std::runtime_error("Failed to populate world resource unit " + typed_scheme->id +
                                 ": static_component.static_mesh_render must not be empty.");
    }
    RUWorld resource_unit;
    resource_unit.static_component.static_mesh_render =
        p_res_chunk.push_data(static_mesh_render.size() + 1, static_mesh_render.c_str());
    return p_res_chunk.push_res_unit(typed_scheme->id, resource_unit).force_cast<RUWorld>();
}

std::span<const std::string> ResourceUnitLoaderScene::get_supported_resource_types() const {
    static const std::array<std::string, 1> supported_resource_types = {"scene"};
    return supported_resource_types;
}

void ResourceUnitLoaderScene::populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const {
    (void)p_res_chunk;
    (void)p_scheme_data;
    throw_loader_not_implemented("scene");
}

std::any ResourceUnitLoaderScene::populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const {
    (void)p_res_chunk;
    (void)p_scheme;
    throw_loader_not_implemented("scene");
}

} // namespace WhiteBirdEngine

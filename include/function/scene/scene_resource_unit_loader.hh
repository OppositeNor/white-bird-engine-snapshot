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
#ifndef WBE_FILE_SCENE_RESOURCE_UNIT_LOADER_HH
#define WBE_FILE_SCENE_RESOURCE_UNIT_LOADER_HH

#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_unit_loader/resource_unit_loader.hh"
#include <any>
#include <span>
#include <string>

namespace WhiteBirdEngine {

/**
 * @class ResourceUnitLoaderComponent
 * @brief Unregistered loader interface for component resources; population is not implemented.
 */
class ResourceUnitLoaderComponent final : public IResourceUnitLoader {
public:
    /** @return A read-only view containing the component resource type name, valid for the lifetime of the program. */
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderPrefab
 * @brief Unregistered loader interface for prefab resources; population is not implemented.
 */
class ResourceUnitLoaderPrefab final : public IResourceUnitLoader {
public:
    /** @return A read-only view containing the prefab resource type name, valid for the lifetime of the program. */
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderWorld
 * @brief Unregistered loader that populates RUWorld resources.
 *
 * Population throws std::runtime_error when the scheme is not an RUSchemaWorld or the static mesh id is empty.
 */
class ResourceUnitLoaderWorld final : public IResourceUnitLoader {
public:
    /** @return A read-only view containing the world resource type name, valid for the lifetime of the program. */
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderScene
 * @brief Unregistered loader interface for scene resources; population is not implemented.
 */
class ResourceUnitLoaderScene final : public IResourceUnitLoader {
public:
    /** @return A read-only view containing the scene resource type name, valid for the lifetime of the program. */
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

} // namespace WhiteBirdEngine

#endif

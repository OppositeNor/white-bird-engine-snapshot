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
#ifndef WBE_FILE_RENDER_VK_RESOURCE_UNIT_LOADER_HH
#define WBE_FILE_RENDER_VK_RESOURCE_UNIT_LOADER_HH

#include "core/data/data_value.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_unit_loader/resource_unit_loader.hh"
#include <any>
#include <span>
#include <string>

namespace WhiteBirdEngine {

// TODO: Test
class ResourceUnitLoaderShader final : public IResourceUnitLoader {
public:
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

// TODO: Test
class ResourceUnitLoaderGraphicsPipeline final : public IResourceUnitLoader {
public:
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderRayTracingPipeline
 * @brief Populates fixed-stage Vulkan ray-tracing pipeline resource units.
 */
class ResourceUnitLoaderRayTracingPipeline final : public IResourceUnitLoader {
public:
    /**
     * @brief Return the ray-tracing pipeline resource type handled by this loader.
     */
    virtual std::span<const std::string> get_supported_resource_types() const override;
    /**
     * @brief Deserialize and populate a ray-tracing pipeline resource.
     */
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

// TODO: Test
class ResourceUnitLoaderMaterial final : public IResourceUnitLoader {
public:
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

class ResourceUnitLoaderMesh final : public IResourceUnitLoader {
public:
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderSkeleton
 * @brief Loads skeletons whose packed joints reference a byte range of a binary resource.
 */
class ResourceUnitLoaderSkeleton final : public IResourceUnitLoader {
public:
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;

    /**
     * @throws std::runtime_error When the joints binary_id is empty or contains NUL, or the joint byte range is empty, is
     * not a multiple of RUSkeleton::JOINT_BYTE_SIZE, or holds more joints than the int32 index range.
     */
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderSDF
 * @brief Loads signed distance fields whose samples are a referenced texture resource.
 *
 * The texture's layout is checked when RUSDF reads it, since references resolve after the resource chunk loads.
 */
class ResourceUnitLoaderSDF final : public IResourceUnitLoader {
public:
    virtual std::span<const std::string> get_supported_resource_types() const override;
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override;

    /**
     * @throws std::runtime_error When texture_id is empty or contains NUL, or the aabb center does not hold 3 values.
     */
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override;
};

void register_renderer_vk_resource_unit_loaders();

} // namespace WhiteBirdEngine

#endif

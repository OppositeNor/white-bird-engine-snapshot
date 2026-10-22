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
#ifndef WBE_FILE_RESOURCE_UNIT_LOADER_HH
#define WBE_FILE_RESOURCE_UNIT_LOADER_HH

#include "core/data/data_value.hh"
#include "core/parser/parser_json.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <any>
#include <array>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WhiteBirdEngine {

/**
 * @class IResourceUnitLoader
 * @brief Dynamic loader interface for resource units.
 */
class IResourceUnitLoader {
public:
    IResourceUnitLoader() = default;
    virtual ~IResourceUnitLoader() = default;
    IResourceUnitLoader(const IResourceUnitLoader&) = default;
    IResourceUnitLoader(IResourceUnitLoader&&) noexcept = default;
    IResourceUnitLoader& operator=(const IResourceUnitLoader&) = default;
    IResourceUnitLoader& operator=(IResourceUnitLoader&&) noexcept = default;

    /**
     * @brief Gets all resource types supported by this loader.
     *
     * @return A read-only view of the supported resource type names. The view remains valid for the lifetime of the program.
     */
    virtual std::span<const std::string> get_supported_resource_types() const = 0;

    /**
     * @brief Populates a scheme into the input chunk.
     *
     * @param p_res_chunk The resource chunk to populate the scheme into.
     * @param p_scheme The scheme to populate.
     */
    void populate_scheme(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const {
        (void)populate_scheme_ptr(p_res_chunk, p_scheme);
    }

    /**
     * @brief Populates a parser node into the input chunk.
     *
     * @param p_res_chunk The resource chunk to populate the scheme into.
     * @param p_scheme_data The parser node to populate.
     */
    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const = 0;

    /**
     * @brief Populates a scheme into the input chunk and returns a type-erased reference to the result.
     *
     * @param p_res_chunk The resource chunk to populate the scheme into.
     * @param p_scheme The scheme to populate.
     * @return A type-erased reference to the resource unit in the chunk.
     */
    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const = 0;
};

template <typename SchemaType>
class ResourceUnitLoaderTyped : public IResourceUnitLoader {
public:
    explicit ResourceUnitLoaderTyped(std::string_view p_resource_type)
        : supported_resource_types{std::string(p_resource_type)}, expected_scheme_type(p_resource_type) {
    }

    virtual std::span<const std::string> get_supported_resource_types() const override {
        return supported_resource_types;
    }

    virtual void populate_scheme_from_data(IResourceChunk& p_res_chunk, const DataValue& p_scheme_data) const override {
        SchemaType scheme;
        scheme.deserialize(p_scheme_data);
        (void)populate_typed_scheme_ptr(p_res_chunk, scheme);
    }

    virtual std::any populate_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchema& p_scheme) const override final {
        const auto* p_typed_scheme = dynamic_cast<const SchemaType*>(&p_scheme);
        if (p_typed_scheme == nullptr) {
            throw std::runtime_error(std::format(
                "Failed to populate resource: expected typed scheme {} but got type {}.", expected_scheme_type, p_scheme.type));
        }
        return populate_typed_scheme_ptr(p_res_chunk, *p_typed_scheme);
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const SchemaType& p_scheme) const = 0;

private:
    std::array<std::string, 1> supported_resource_types;
    std::string expected_scheme_type;
};

class ResourceUnitLoaderAudio final : public ResourceUnitLoaderTyped<RUSchemaAudio> {
public:
    ResourceUnitLoaderAudio() : ResourceUnitLoaderTyped("audio") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaAudio& p_scheme) const override;
};

class ResourceUnitLoaderBinary final : public ResourceUnitLoaderTyped<RUSchemaBinary> {
public:
    ResourceUnitLoaderBinary() : ResourceUnitLoaderTyped("binary") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaBinary& p_scheme) const override;
};

class ResourceUnitLoaderFont final : public ResourceUnitLoaderTyped<RUSchemaFont> {
public:
    ResourceUnitLoaderFont() : ResourceUnitLoaderTyped("font") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaFont& p_scheme) const override;
};

class ResourceUnitLoaderImage final : public ResourceUnitLoaderTyped<RUSchemaImage> {
public:
    ResourceUnitLoaderImage() : ResourceUnitLoaderTyped("image") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaImage& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderTexture
 * @brief Loads KTX 2 texture resources.
 */
class ResourceUnitLoaderTexture final : public ResourceUnitLoaderTyped<RUSchemaTexture> {
public:
    ResourceUnitLoaderTexture() : ResourceUnitLoaderTyped("texture") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaTexture& p_scheme) const override;
};

class ResourceUnitLoaderText final : public ResourceUnitLoaderTyped<RUSchemaText> {
public:
    ResourceUnitLoaderText() : ResourceUnitLoaderTyped("text") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaText& p_scheme) const override;
};

/**
 * @class ResourceUnitLoaderScript
 * @brief Loads script resources: the source file becomes chunk-owned text of an RUScript.
 */
class ResourceUnitLoaderScript final : public ResourceUnitLoaderTyped<RUSchemaScript> {
public:
    ResourceUnitLoaderScript() : ResourceUnitLoaderTyped("script") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaScript& p_scheme) const override;
};

class ResourceUnitLoaderVideo final : public ResourceUnitLoaderTyped<RUSchemaVideo> {
public:
    ResourceUnitLoaderVideo() : ResourceUnitLoaderTyped("video") {
    }

protected:
    virtual std::any populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaVideo& p_scheme) const override;
};

} // namespace WhiteBirdEngine

#endif

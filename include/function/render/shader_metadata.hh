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
#ifndef WBE_FILE_SHADER_METADATA_HH
#define WBE_FILE_SHADER_METADATA_HH

#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include <cstdint>
#include <string>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class ShaderMetadataUserAttributeArgument
 * @brief One typed argument supplied to a reflected Slang user attribute.
 *
 * The kind is `int`, `float`, or `string`. Only the value field selected by kind is meaningful.
 */
struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataUserAttributeArgument final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataUserAttributeArgument)

    /**
     * @brief Normalized argument kind.
     */
    WBE_META(WBE_REFLECT)
    std::string kind;
    /**
     * @brief Integer argument value when kind is `int`.
     */
    WBE_META(WBE_REFLECT)
    int64_t integer_value = 0;
    /**
     * @brief Floating-point argument value when kind is `float`.
     */
    WBE_META(WBE_REFLECT)
    double floating_point_value = 0.0;
    /**
     * @brief String argument value when kind is `string`.
     */
    WBE_META(WBE_REFLECT)
    std::string string_value;
};

/**
 * @class ShaderMetadataUserAttribute
 * @brief Slang user attribute reflected on a shader declaration.
 *
 * Attribute names and argument order are preserved from the source declaration.
 */
struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataUserAttribute final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataUserAttribute)

    /**
     * @brief Attribute type name without the `Attribute` suffix applied by Slang syntax.
     */
    WBE_META(WBE_REFLECT)
    std::string name;
    /**
     * @brief Ordered arguments supplied to the attribute.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataUserAttributeArgument> arguments;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataBinding final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataBinding)

    WBE_META(WBE_REFLECT)
    std::string kind;
    WBE_META(WBE_REFLECT)
    int32_t index = 0;
    WBE_META(WBE_REFLECT)
    int32_t count = 0;
    WBE_META(WBE_REFLECT)
    int32_t space = 0;
    WBE_META(WBE_REFLECT)
    int32_t offset = 0;
    WBE_META(WBE_REFLECT)
    int32_t size = 0;
    WBE_META(WBE_REFLECT)
    int32_t element_stride = 0;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataTypeAtom final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataTypeAtom)

    WBE_META(WBE_REFLECT)
    std::string kind;
    WBE_META(WBE_REFLECT)
    std::string scalar_type;
    WBE_META(WBE_REFLECT)
    std::string access;
    WBE_META(WBE_REFLECT)
    std::string name;
    WBE_META(WBE_REFLECT)
    int32_t element_count = 0;
    WBE_META(WBE_REFLECT)
    std::string element_scalar_type;
    WBE_META(WBE_REFLECT)
    std::string element_access;
    /**
     * @brief User attributes reflected from this type declaration.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataUserAttribute> user_attributes;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataTypeField final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataTypeField)

    WBE_META(WBE_REFLECT)
    std::string name;
    WBE_META(WBE_REFLECT)
    ShaderMetadataTypeAtom type;
    WBE_META(WBE_REFLECT)
    std::string stage;
    WBE_META(WBE_REFLECT)
    ShaderMetadataBinding binding;
    WBE_META(WBE_REFLECT)
    int32_t buffer_size = 0;
    WBE_META(WBE_REFLECT)
    std::string semantic_name;
    WBE_META(WBE_REFLECT)
    int32_t semantic_index = 0;
    /**
     * @brief User attributes reflected from this field declaration.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataUserAttribute> user_attributes;
    /**
     * @brief Fields nested inside this field's struct or array-element type.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataTypeField> fields;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataType final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataType)

    WBE_META(WBE_REFLECT)
    std::string kind;
    WBE_META(WBE_REFLECT)
    std::string name;
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataTypeField> fields;
    WBE_META(WBE_REFLECT)
    int32_t element_count = 0;
    WBE_META(WBE_REFLECT)
    ShaderMetadataTypeAtom element_type;
    WBE_META(WBE_REFLECT)
    std::string scalar_type;
    WBE_META(WBE_REFLECT)
    std::string access;
    WBE_META(WBE_REFLECT)
    std::string element_access;
    /**
     * @brief User attributes reflected from this type declaration.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataUserAttribute> user_attributes;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataParameter final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataParameter)

    WBE_META(WBE_REFLECT)
    std::string name;
    WBE_META(WBE_REFLECT)
    std::string stage;
    WBE_META(WBE_REFLECT)
    ShaderMetadataBinding binding;
    WBE_META(WBE_REFLECT)
    ShaderMetadataType type;
    WBE_META(WBE_REFLECT)
    std::string semantic_name;
    WBE_META(WBE_REFLECT)
    int32_t semantic_index = 0;
    /**
     * @brief User attributes reflected from this variable or parameter declaration.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataUserAttribute> user_attributes;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataResult final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataResult)

    WBE_META(WBE_REFLECT)
    std::string stage;
    WBE_META(WBE_REFLECT)
    ShaderMetadataBinding binding;
    WBE_META(WBE_REFLECT)
    std::string semantic_name;
    WBE_META(WBE_REFLECT)
    int32_t semantic_index = 0;
    WBE_META(WBE_REFLECT)
    ShaderMetadataType type;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadataEntryPoint final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadataEntryPoint)

    WBE_META(WBE_REFLECT)
    std::string name;
    WBE_META(WBE_REFLECT)
    std::string stage;
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataParameter> parameters;
    WBE_META(WBE_REFLECT)
    ShaderMetadataResult result;
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataBinding> bindings;
    /**
     * @brief User attributes reflected from this entry-point function.
     */
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataUserAttribute> user_attributes;
};

struct WBE_META(WBE_SERIALIZABLE) ShaderMetadata final : public Serializable {
    WBE_DECL_SERIALIZABLE(ShaderMetadata)

    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataParameter> parameters;
    WBE_META(WBE_REFLECT)
    std::vector<ShaderMetadataEntryPoint> entry_points;
};
} // namespace WhiteBirdEngine

#endif

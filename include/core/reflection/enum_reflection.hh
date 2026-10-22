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
#ifndef WBE_FILE_ENUM_REFLECTION_HH
#define WBE_FILE_ENUM_REFLECTION_HH

#include "core/data/data_value.hh"
#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable_sd.hh"
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace WhiteBirdEngine {

/**
 * @class EnumReflection
 * @brief Name and value reflection of an enum annotated with WBE_META(WBE_REFLECT_ENUM).
 *
 * The specializations are generated in generated/enum_reflection.gen.hh; include it to use them. A specialization derives
 * from EnumReflectionBase, which documents the available methods. Enumerators may declare additional accepted names with
 * WBE_META(WBE_SERIALIZATION_ALIASES(name, ...)) after the enumerator name; an enumerator marked
 * WBE_META(WBE_REFLECT_ENUM_IGNORE), such as a count sentinel, has no reflected name and is not a reflected value. A header
 * that declares a reflected enum must not include the generated header, which includes every such header.
 *
 * @tparam TEnum The reflected enum.
 */
template <typename TEnum>
struct EnumReflection;

/**
 * @brief Whether an enum has a generated EnumReflection specialization visible.
 */
template <typename TEnum>
concept ReflectedEnumConcept = std::is_enum_v<TEnum> && requires { EnumReflection<TEnum>::TYPE_NAME; };

/**
 * @class EnumReflectionEntry
 * @brief One accepted name of an enum value. Copyable.
 */
template <typename TEnum>
struct EnumReflectionEntry {
    /**
     * @brief The enumerator name or alias.
     */
    std::string_view name;
    /**
     * @brief The value the name stands for.
     */
    TEnum value;
};

/**
 * @class EnumReflectionBase
 * @brief The lookups shared by every generated EnumReflection specialization.
 *
 * TDerived provides TYPE_NAME, ENUMERATORS (every enumerator under its own name, in declaration order) and NAMES (every
 * enumerator name and alias). Values are compared and range-checked as the enum's underlying type. When several
 * enumerators share a value, the first declared one is its canonical name. Methods without the try_ prefix throw
 * std::runtime_error when they cannot convert; the try_ methods return std::nullopt instead. Stateless; every method may be
 * called from any thread.
 *
 * @tparam TEnum The reflected enum.
 * @tparam TDerived The generated EnumReflection specialization.
 */
template <typename TEnum, typename TDerived>
struct EnumReflectionBase {
    /**
     * @brief The reflected enum.
     */
    using EnumType = TEnum;
    /**
     * @brief The underlying integer type of the enum.
     */
    using UnderlyingType = std::underlying_type_t<TEnum>;

    /**
     * @brief Convert a value to its underlying integer.
     */
    static constexpr UnderlyingType to_underlying(TEnum p_value) {
        return static_cast<UnderlyingType>(p_value);
    }

    /**
     * @brief Get the canonical name of a value.
     * @return The name of the first declared enumerator with the value, or std::nullopt if no enumerator has it.
     */
    static constexpr std::optional<std::string_view> try_enum_to_string(TEnum p_value) {
        for (const EnumReflectionEntry<TEnum>& entry : TDerived::ENUMERATORS) {
            if (entry.value == p_value) {
                return entry.name;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Get the canonical name of a value.
     * @throws std::runtime_error If no enumerator has the value.
     */
    static std::string_view enum_to_string(TEnum p_value) {
        const std::optional<std::string_view> name = try_enum_to_string(p_value);
        if (!name.has_value()) {
            throw std::runtime_error(std::format("Failed to get the name of {} value {}: no enumerator has this value.",
                TDerived::TYPE_NAME,
                format_underlying(to_underlying(p_value))));
        }
        return *name;
    }

    /**
     * @brief Get the value of an enumerator name or alias. Names are case-sensitive.
     * @return The value, or std::nullopt if the name is unknown.
     */
    static constexpr std::optional<TEnum> try_string_to_enum(std::string_view p_name) {
        for (const EnumReflectionEntry<TEnum>& entry : TDerived::NAMES) {
            if (entry.name == p_name) {
                return entry.value;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Get the value of an enumerator name or alias. Names are case-sensitive.
     * @throws std::runtime_error If the name is unknown.
     */
    static TEnum string_to_enum(std::string_view p_name) {
        const std::optional<TEnum> value = try_string_to_enum(p_name);
        if (!value.has_value()) {
            throw std::runtime_error(
                std::format("Failed to convert '{}' to {}: no enumerator or alias has this name.", p_name, TDerived::TYPE_NAME));
        }
        return *value;
    }

    /**
     * @brief Convert an underlying integer to the value of a declared enumerator.
     * @return The value, or std::nullopt if no enumerator has it.
     */
    static constexpr std::optional<TEnum> try_from_underlying(UnderlyingType p_underlying) {
        for (const EnumReflectionEntry<TEnum>& entry : TDerived::ENUMERATORS) {
            if (to_underlying(entry.value) == p_underlying) {
                return entry.value;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Convert an underlying integer to the value of a declared enumerator.
     * @throws std::runtime_error If no enumerator has the value.
     */
    static TEnum from_underlying(UnderlyingType p_underlying) {
        const std::optional<TEnum> value = try_from_underlying(p_underlying);
        if (!value.has_value()) {
            throw std::runtime_error(std::format(
                "Failed to convert {} to {}: no enumerator has this value.", format_underlying(p_underlying), TDerived::TYPE_NAME));
        }
        return *value;
    }

    /**
     * @brief Get every enumerator under its own name, in declaration order.
     */
    static constexpr std::span<const EnumReflectionEntry<TEnum>> get_enumerators() {
        return TDerived::ENUMERATORS;
    }

    /**
     * @brief Get every accepted name: enumerator names and aliases.
     */
    static constexpr std::span<const EnumReflectionEntry<TEnum>> get_names() {
        return TDerived::NAMES;
    }

private:
    // Character-sized underlying types would otherwise format as characters.
    static auto format_underlying(UnderlyingType p_underlying) {
        if constexpr (std::is_signed_v<UnderlyingType>) {
            return static_cast<int64_t>(p_underlying);
        } else {
            return static_cast<uint64_t>(p_underlying);
        }
    }
};

/**
 * @class SerializableSD
 * @brief Serializer/deserializer for reflected enums.
 *
 * A value is serialized as its canonical name. It deserializes from an enumerator name or alias, or from an integer that
 * must fit the underlying type and equal a declared enumerator; anything else throws std::runtime_error.
 *
 * @tparam TEnum The reflected enum.
 */
template <ReflectedEnumConcept TEnum>
class SerializableSD<TEnum> {
public:
    using ObjType = TEnum;

    /**
     * @brief Serialize the canonical name.
     * @throws std::runtime_error If no enumerator has the value.
     */
    static void serialize(DataValue& p_data, const ObjType& p_serializable) {
        p_data.set(std::string(EnumReflection<TEnum>::enum_to_string(p_serializable)));
    }

    /**
     * @brief Deserialize a name, an alias, or an underlying integer.
     * @throws std::runtime_error If the data is not a known name, or not an integer equal to a declared enumerator.
     */
    static void deserialize(const DataValue& p_data, ObjType& p_serializable) {
        if (p_data.is_string()) {
            p_serializable = EnumReflection<TEnum>::string_to_enum(p_data.get<std::string>());
            return;
        }
        using UnderlyingType = EnumReflection<TEnum>::UnderlyingType;
        // Read through the widest integer of the same signedness as the underlying type, then range-check.
        using WideType = std::conditional_t<std::is_same_v<UnderlyingType, uint64_t>, uint64_t, int64_t>;
        const WideType integer = p_data.get<WideType>();
        if (!std::in_range<UnderlyingType>(integer)) {
            throw std::runtime_error(std::format(
                "Failed to convert {} to {}: the value does not fit the underlying type.", integer, EnumReflection<TEnum>::TYPE_NAME));
        }
        p_serializable = EnumReflection<TEnum>::from_underlying(static_cast<UnderlyingType>(integer));
    }
};

} // namespace WhiteBirdEngine

#endif

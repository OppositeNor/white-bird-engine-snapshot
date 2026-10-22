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
#ifndef WBE_FILE_DATA_VALUE_HH
#define WBE_FILE_DATA_VALUE_HH

#include "glm/detail/qualifier.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "utils/utils.hh"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @brief Whether a type is a std::vector, which DataValue converts element by element.
 */
template <typename T>
concept DataValueStdVectorConcept = requires { typename T::value_type; } && std::same_as<T, std::vector<typename T::value_type>>;

/**
 * @enum DataValueKind
 * @brief The kinds of values a DataValue can hold.
 */
enum class DataValueKind : uint8_t {
    NIL,
    BOOLEAN,
    INTEGER,
    FLOAT,
    STRING,
    LIST,
    MAP,
};

/**
 * @class DataValue
 * @brief Format-neutral structured data: the value tree every parser, serializer, and script backend exchanges.
 *
 * A value is nil, a boolean, a signed 64-bit integer, a double, a string, a list of values, or a map from string keys to
 * values. Maps keep their entries sorted by key, so iteration order is deterministic. The value is a deep, self-contained
 * tree that owns no format or backend state; it is copyable and movable, and a moved-from value is nil. It uses standard
 * containers.
 *
 * Formats are the parsers' concern: ParserJSON and ParserTOML read text into a DataValue and write it back, and script
 * backends convert their values to and from it. Reflection-generated SerializableSD specializations read and write it.
 * Reads convert between integers and floats when the conversion is exact, and an empty list and an empty map read as each
 * other. Accessors throw std::runtime_error when the held kind does not match.
 */
class DataValue final {
public:
    /**
     * @brief List storage.
     */
    using List = std::vector<DataValue>;
    /**
     * @brief One map entry.
     */
    using MapEntry = std::pair<std::string, DataValue>;
    /**
     * @brief Map storage, sorted by key with unique keys.
     */
    using Map = std::vector<MapEntry>;

    DataValue() = default;
    ~DataValue() = default;
    DataValue(const DataValue&) = default;
    DataValue(DataValue&& p_other) noexcept : value(std::exchange(p_other.value, std::monostate{})) {
    }
    DataValue& operator=(const DataValue&) = default;
    DataValue& operator=(DataValue&& p_other) noexcept {
        if (this != &p_other) {
            value = std::exchange(p_other.value, std::monostate{});
        }
        return *this;
    }

    /**
     * @brief Create a boolean value.
     */
    static DataValue make_boolean(bool p_value);
    /**
     * @brief Create an integer value.
     */
    static DataValue make_integer(int64_t p_value);
    /**
     * @brief Create a float value.
     */
    static DataValue make_float(double p_value);
    /**
     * @brief Create a string value.
     */
    static DataValue make_string(std::string_view p_value);
    /**
     * @brief Create an empty list.
     */
    static DataValue make_list();
    /**
     * @brief Create an empty map.
     */
    static DataValue make_map();

    /**
     * @brief Get the name of a kind, e.g. "integer".
     */
    static std::string_view get_kind_name(DataValueKind p_kind);

    /**
     * @brief Get the kind of the held value.
     */
    DataValueKind get_kind() const {
        return static_cast<DataValueKind>(value.index());
    }
    /**
     * @brief Check whether the value is nil.
     */
    bool is_nil() const {
        return get_kind() == DataValueKind::NIL;
    }
    /**
     * @brief Get the held boolean.
     * @throws std::runtime_error If the value is not a boolean.
     */
    bool as_boolean() const;
    /**
     * @brief Get the held integer.
     * @throws std::runtime_error If the value is not an integer.
     */
    int64_t as_integer() const;
    /**
     * @brief Get the held number as a double; integers convert.
     * @throws std::runtime_error If the value is neither a float nor an integer.
     */
    double as_float() const;
    /**
     * @brief Get the held string.
     * @throws std::runtime_error If the value is not a string.
     */
    const std::string& as_string() const;
    /**
     * @brief Get the held list.
     * @throws std::runtime_error If the value is not a list.
     */
    const List& as_list() const;
    /**
     * @brief Get the held list; an empty map reads as an empty list.
     * @throws std::runtime_error If the value is neither a list nor an empty map.
     */
    const List& as_list_or_empty() const;
    /**
     * @brief Get the held map entries, sorted by key.
     * @throws std::runtime_error If the value is not a map.
     */
    const Map& as_map() const;
    /**
     * @brief Append an element. A nil value becomes an empty list first.
     * @throws std::runtime_error If the value is neither nil nor a list.
     */
    void push_back(DataValue p_element);
    /**
     * @brief Insert or replace a map entry. A nil value, or an empty list, becomes an empty map first.
     * @throws std::runtime_error If the value is neither nil, an empty list, nor a map.
     */
    void set_entry(std::string_view p_key, DataValue p_entry);
    /**
     * @brief Find a map entry.
     * @return The entry, or null when the value is not a map or has no entry for the key.
     */
    const DataValue* find(std::string_view p_key) const;
    /**
     * @brief Get a map entry.
     * @throws std::runtime_error If the value is not a map or has no entry for the key.
     */
    const DataValue& get_entry(std::string_view p_key) const;

    /**
     * @brief Compare two values structurally. Integers and floats are different kinds and never compare equal.
     */
    bool operator==(const DataValue& p_other) const;

    /**
     * @brief Get the keys of a map value in sorted order; other kinds have no keys.
     */
    std::vector<std::string> get_all_keys() const;
    /**
     * @brief Check whether a map value has an entry for the key; other kinds contain no keys.
     */
    bool contains(std::string_view p_key) const {
        return find(p_key) != nullptr;
    }
    /**
     * @brief Check whether the value is a string.
     */
    bool is_string() const {
        return get_kind() == DataValueKind::STRING;
    }

    /**
     * @brief Convert a C++ value and store it under a key. A nil value becomes a map first.
     * @throws std::runtime_error Under the conditions of set() and set_entry().
     */
    template <typename T>
    void set_value(std::string_view p_key, T&& p_value) {
        DataValue entry;
        entry.set(std::forward<T>(p_value));
        set_entry(p_key, std::move(entry));
    }

    /**
     * @brief Replace this value with a converted C++ value.
     *
     * Supports DataValue, std::nullptr_t (nil), bool, integral and floating-point types, enums, strings, Buffer, glm::vec2,
     * glm::vec3, glm::vec4, glm::quat, glm::mat4, and std::vector of any supported type, which becomes a list. Vectors and
     * quaternions become maps of `x`, `y`, `z`, `w`; matrices become a list of 16 column-major floats.
     * @throws std::runtime_error If an unsigned integer exceeds the signed 64-bit range.
     */
    template <typename T>
    void set(T&& p_value) { // NOLINT
        using Type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<Type, DataValue>) {
            *this = std::forward<T>(p_value);
        } else if constexpr (std::same_as<Type, List>) {
            value = List(std::forward<T>(p_value));
        } else if constexpr (std::same_as<Type, std::nullptr_t>) {
            value = std::monostate{};
        } else if constexpr (std::same_as<Type, bool>) {
            value = p_value;
        } else if constexpr (std::is_enum_v<Type>) {
            set(static_cast<std::underlying_type_t<Type>>(p_value));
        } else if constexpr (std::integral<Type>) {
            if (!std::in_range<int64_t>(p_value)) {
                throw std::runtime_error(std::format("Failed to set data value: integer {} exceeds the signed 64-bit range.", p_value));
            }
            value = static_cast<int64_t>(p_value);
        } else if constexpr (std::floating_point<Type>) {
            value = static_cast<double>(p_value);
        } else if constexpr (std::convertible_to<const Type&, std::string_view>) {
            value = std::string(std::string_view(p_value));
        } else if constexpr (BufferBaseConcept<Type>) {
            value = std::string(p_value.buffer, strnlen(p_value.buffer, Type::BUFFER_SIZE));
        } else if constexpr (std::same_as<Type, glm::vec2>) {
            set_vector_components(&p_value.x, 2);
        } else if constexpr (std::same_as<Type, glm::vec3>) {
            set_vector_components(&p_value.x, 3);
        } else if constexpr (std::same_as<Type, glm::vec4> || std::same_as<Type, glm::quat>) {
            set_vector_components(&p_value.x, 4);
        } else if constexpr (std::same_as<Type, glm::mat4>) {
            List matrix;
            matrix.reserve(16);
            for (glm::length_t column = 0; column < 4; ++column) {
                for (glm::length_t row = 0; row < 4; ++row) {
                    matrix.push_back(make_float(p_value[column][row]));
                }
            }
            value = std::move(matrix);
        } else if constexpr (DataValueStdVectorConcept<Type>) {
            List list;
            list.reserve(p_value.size());
            for (const auto& element : p_value) {
                DataValue element_value;
                element_value.set(element);
                list.push_back(std::move(element_value));
            }
            value = std::move(list);
        } else {
            static_assert(sizeof(Type) == 0, "Unsupported type for DataValue::set.");
        }
    }

    /**
     * @brief Convert the value stored under a key.
     * @throws std::runtime_error If the key is missing or the entry cannot be converted.
     */
    template <typename T>
    T get_value(std::string_view p_key) const {
        T result{};
        get_value<T>(p_key, result);
        return result;
    }

    /**
     * @brief Convert this value.
     * @throws std::runtime_error Under the conditions of get(T&).
     */
    template <typename T>
    T get() const {
        T result{};
        get<T>(result);
        return result;
    }

    /**
     * @brief Convert this value into p_value; supports the types listed for set().
     *
     * Integral targets accept integers and floats with an exact integral value in range. Vectors accept maps of `x`, `y`,
     * `z`, `w`; glm::vec2 also accepts `u`, `v`. std::nullptr_t accepts only nil. Strings longer than a Buffer's capacity
     * are rejected.
     * @throws std::runtime_error If the value has an incompatible kind or is out of range.
     */
    template <typename T>
    void get(T& p_value) const { // NOLINT
        using Type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<Type, DataValue>) {
            p_value = *this;
        } else if constexpr (std::same_as<Type, List>) {
            p_value = as_list_or_empty();
        } else if constexpr (std::same_as<Type, std::nullptr_t>) {
            if (!is_nil()) {
                throw_kind_mismatch("nil");
            }
            p_value = nullptr;
        } else if constexpr (std::same_as<Type, bool>) {
            p_value = as_boolean();
        } else if constexpr (std::is_enum_v<Type>) {
            p_value = static_cast<Type>(get_integral<std::underlying_type_t<Type>>());
        } else if constexpr (std::integral<Type>) {
            p_value = get_integral<Type>();
        } else if constexpr (std::floating_point<Type>) {
            p_value = static_cast<Type>(as_float());
        } else if constexpr (std::same_as<Type, std::string>) {
            p_value = as_string();
        } else if constexpr (BufferBaseConcept<Type>) {
            const std::string& string_value = as_string();
            if (string_value.size() > Type::BUFFER_SIZE - 1) {
                throw std::runtime_error(std::format(
                    "Failed to get data value: string of length {} exceeds buffer capacity {}.", string_value.size(), Type::BUFFER_SIZE));
            }
            std::memcpy(p_value.buffer, string_value.data(), string_value.size());
            p_value.buffer[string_value.size()] = '\0';
        } else if constexpr (std::same_as<Type, glm::vec2>) {
            if (contains("u")) {
                p_value = glm::vec2(get_value<float>("u"), get_value<float>("v"));
            } else {
                p_value = glm::vec2(get_value<float>("x"), get_value<float>("y"));
            }
        } else if constexpr (std::same_as<Type, glm::vec3>) {
            p_value = glm::vec3(get_value<float>("x"), get_value<float>("y"), get_value<float>("z"));
        } else if constexpr (std::same_as<Type, glm::vec4>) {
            p_value = glm::vec4(get_value<float>("x"), get_value<float>("y"), get_value<float>("z"), get_value<float>("w"));
        } else if constexpr (std::same_as<Type, glm::quat>) {
            p_value = glm::quat(get_value<float>("w"), get_value<float>("x"), get_value<float>("y"), get_value<float>("z"));
        } else if constexpr (std::same_as<Type, glm::mat4>) {
            const List& matrix = as_list();
            if (matrix.size() != 16) {
                throw std::runtime_error(
                    std::format("Failed to get glm::mat4 from data value: expected 16 values but got {}.", matrix.size()));
            }
            for (glm::length_t column = 0; column < 4; ++column) {
                for (glm::length_t row = 0; row < 4; ++row) {
                    p_value[column][row] = static_cast<float>(matrix[(column * 4) + row].as_float());
                }
            }
        } else if constexpr (DataValueStdVectorConcept<Type>) {
            const List& list = as_list_or_empty();
            p_value.clear();
            p_value.reserve(list.size());
            for (const DataValue& element : list) {
                typename Type::value_type element_value{};
                element.get(element_value);
                p_value.push_back(std::move(element_value));
            }
        } else {
            static_assert(sizeof(Type) == 0, "Unsupported type for DataValue::get.");
        }
    }

    /**
     * @brief Convert the value stored under a key into p_value.
     * @throws std::runtime_error If the key is missing or the entry cannot be converted.
     */
    template <typename T>
    void get_value(std::string_view p_key, T& p_value) const {
        get_entry(p_key).get(p_value);
    }

private:
    template <std::integral TIntegral>
    TIntegral get_integral() const {
        int64_t integer = 0;
        if (get_kind() == DataValueKind::FLOAT) {
            integer = float_to_exact_integer(std::get<double>(value));
        } else {
            integer = as_integer();
        }
        if (!std::in_range<TIntegral>(integer)) {
            throw std::runtime_error(std::format("Failed to get data value: integer {} is out of range for the target type.", integer));
        }
        return static_cast<TIntegral>(integer);
    }

    static int64_t float_to_exact_integer(double p_value);
    void set_vector_components(const float* p_components, size_t p_count);
    [[noreturn]] void throw_kind_mismatch(std::string_view p_expected) const;

    std::variant<std::monostate, bool, int64_t, double, std::string, List, Map> value;
};

} // namespace WhiteBirdEngine

#endif

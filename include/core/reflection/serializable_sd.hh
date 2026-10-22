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
#ifndef WBE_FILE_SERIALIZABLE_SD_HH
#define WBE_FILE_SERIALIZABLE_SD_HH

#include "core/data/data_value.hh"
#include <optional>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

/**
 * @class SerializableSD
 * @brief Converts objects of a type to and from DataValue.
 *
 * The primary template converts the types DataValue::set() and DataValue::get() support. Reflection generates a
 * specialization for every serializable class (generated/serializables_sd.gen.hh), and enum_reflection.hh specializes it for
 * reflected enums.
 *
 * @tparam T The type to serialize/deserialize.
 */
template <typename T>
class SerializableSD {
public:
    using ObjType = T;

    /**
     * @brief Serialize p_serializable into p_data.
     */
    static void serialize(DataValue& p_data, const ObjType& p_serializable) {
        p_data.set(p_serializable);
    }

    /**
     * @brief Deserialize p_data into p_serializable.
     * @throws std::runtime_error If p_data cannot be converted.
     */
    static void deserialize(const DataValue& p_data, ObjType& p_serializable) {
        p_data.get<ObjType>(p_serializable);
    }
};

/**
 * @class SerializableSD
 * @brief Converts optional values; an empty optional is not serialized.
 */
template <typename T>
class SerializableSD<std::optional<T>> {
public:
    using ObjType = std::optional<T>;

    static void serialize(DataValue& p_data, const ObjType& p_serializable) {
        if (p_serializable.has_value()) {
            SerializableSD<T>::serialize(p_data, p_serializable.value());
        }
    }

    static void deserialize(const DataValue& p_data, ObjType& p_serializable) {
        T result{};
        SerializableSD<T>::deserialize(p_data, result);
        p_serializable = std::move(result);
    }
};

/**
 * @class SerializableSD
 * @brief Converts vectors element by element, so elements use their own SerializableSD.
 */
template <typename T>
class SerializableSD<std::vector<T>> {
public:
    using ObjType = std::vector<T>;

    static void serialize(DataValue& p_data, const ObjType& p_serializable) {
        DataValue result = DataValue::make_list();
        for (const T& element : p_serializable) {
            DataValue element_data;
            SerializableSD<T>::serialize(element_data, element);
            result.push_back(std::move(element_data));
        }
        p_data = std::move(result);
    }

    static void deserialize(const DataValue& p_data, ObjType& p_serializable) {
        p_serializable.clear();
        const DataValue::List& elements = p_data.as_list_or_empty();
        p_serializable.reserve(elements.size());
        for (const DataValue& element_data : elements) {
            T element{};
            SerializableSD<T>::deserialize(element_data, element);
            p_serializable.push_back(std::move(element));
        }
    }
};

} // namespace WhiteBirdEngine

#endif

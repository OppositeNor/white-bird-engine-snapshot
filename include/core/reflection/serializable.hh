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
#ifndef WBE_FILE_SERIALIZABLE_HH
#define WBE_FILE_SERIALIZABLE_HH

#include "core/data/data_value.hh"
#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable_sd.hh"

#define WBE_DECL_SERIALIZABLE(class_name)                                                                                   \
    friend class ::WhiteBirdEngine::SerializableSD<class_name>;                                                             \
    virtual void serialize(::WhiteBirdEngine::DataValue& p_data) const override;                                            \
    virtual void deserialize(const ::WhiteBirdEngine::DataValue& p_data) override;

namespace WhiteBirdEngine {

/**
 * @class Serializable
 * @brief Serializable struct.
 *
 */
class WBE_META(WBE_SERIALIZABLE) Serializable {
public:
    Serializable() = default;
    virtual ~Serializable() = default;
    Serializable(const Serializable&) = default;
    Serializable(Serializable&&) = default;
    Serializable& operator=(const Serializable&) = default;
    Serializable& operator=(Serializable&&) = default;

    /**
     * @brief Serialize.
     *
     * @param p_data The data to store serialization.
     */
    virtual void serialize(DataValue& p_data) const = 0;

    /**
     * @brief Deserialize.
     *
     * @param p_data The data to find data from.
     */
    virtual void deserialize(const DataValue& p_data) = 0;
};

} // namespace WhiteBirdEngine

#endif

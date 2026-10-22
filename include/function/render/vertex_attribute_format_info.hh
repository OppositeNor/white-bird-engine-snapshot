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
#ifndef WBE_FILE_VERTEX_ATTRIBUTE_FORMAT_INFO_HH
#define WBE_FILE_VERTEX_ATTRIBUTE_FORMAT_INFO_HH

#include "function/render/render_resource_unit.hh"
#include <cstddef>

namespace WhiteBirdEngine {

/**
 * @class VertexAttributeFormatInfo
 * @brief Canonical properties of a supported resource vertex format.
 */
class VertexAttributeFormatInfo final {
public:
    /**
     * @brief Return the tightly packed byte size of one attribute value.
     * @throws std::runtime_error When the format is unsupported.
     */
    static size_t get_size(RUVertexAttributeFormat p_format);

private:
    VertexAttributeFormatInfo() = delete;
    ~VertexAttributeFormatInfo() = delete;
    VertexAttributeFormatInfo(const VertexAttributeFormatInfo&) = delete;
    VertexAttributeFormatInfo(VertexAttributeFormatInfo&&) noexcept = delete;
    VertexAttributeFormatInfo& operator=(const VertexAttributeFormatInfo&) = delete;
    VertexAttributeFormatInfo& operator=(VertexAttributeFormatInfo&&) noexcept = delete;
};

} // namespace WhiteBirdEngine

#endif

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
#include "function/render/vertex_attribute_format_info.hh"
#include "function/render/render_resource_unit.hh"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace WhiteBirdEngine {

size_t VertexAttributeFormatInfo::get_size(RUVertexAttributeFormat p_format) {
    switch (p_format) {
    case RUVertexAttributeFormat::FLOAT:
    case RUVertexAttributeFormat::INT:
    case RUVertexAttributeFormat::UINT:
        return sizeof(uint32_t);
    case RUVertexAttributeFormat::FLOAT2:
    case RUVertexAttributeFormat::INT2:
    case RUVertexAttributeFormat::UINT2:
        return sizeof(uint32_t) * 2;
    case RUVertexAttributeFormat::FLOAT3:
    case RUVertexAttributeFormat::INT3:
    case RUVertexAttributeFormat::UINT3:
        return sizeof(uint32_t) * 3;
    case RUVertexAttributeFormat::FLOAT4:
    case RUVertexAttributeFormat::INT4:
    case RUVertexAttributeFormat::UINT4:
        return sizeof(uint32_t) * 4;
    }
    throw std::runtime_error("Failed to inspect vertex attribute format: unsupported format.");
}

} // namespace WhiteBirdEngine

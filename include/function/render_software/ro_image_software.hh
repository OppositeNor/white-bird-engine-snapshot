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
#ifndef WBE_FILE_RO_IMAGE_SOFTWARE
#define WBE_FILE_RO_IMAGE_SOFTWARE

#include "core/allocator/stl_allocator.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_defs.hh"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <span>
namespace WhiteBirdEngine {

/**
 * @class
 * @brief Image
 */
class WBE_META(WBE_RENDER_OBJECT) ROImageSoftware : public IRenderObject {
public:
    ROImageSoftware() = delete;
    virtual ~ROImageSoftware() override = default;
    ROImageSoftware(const ROImageSoftware&) = delete;
    ROImageSoftware(ROImageSoftware&&) = delete;
    ROImageSoftware& operator=(const ROImageSoftware&) = delete;
    ROImageSoftware& operator=(ROImageSoftware&&) = delete;

    virtual TypeID get_type_uuid() const override;

    enum class Format {
        R8G8B8A8_UNORM,
        R32_SFLOAT,
    };

    ROImageSoftware(glm::uvec2 p_extent, uint32_t p_mipmap_lod = 1);

    uint32_t get_row_pitch(uint32_t p_mip_level = 0) const noexcept {
        return mip_infos[p_mip_level].row_pitch;
    }

    uint8_t* get_buffer_data() noexcept {
        return buffer.data();
    }

    const uint8_t* get_buffer_data() const noexcept {
        return buffer.data();
    }

    glm::vec4 fetch_pixel(glm::uvec2 p_pos, uint32_t p_mip_level) noexcept {
        const auto& info = mip_infos[p_mip_level];
        size_t offset = get_offset(p_pos, info);
        const uint8_t* ptr = info.memory.data() + offset;
        glm::vec4 color{};
        switch (format) {
        case Format::R8G8B8A8_UNORM:
            color.r = static_cast<float>(ptr[0]) / 255.0F;
            color.g = static_cast<float>(ptr[1]) / 255.0F;
            color.b = static_cast<float>(ptr[2]) / 255.0F;
            color.a = static_cast<float>(ptr[3]) / 255.0F;
            break;
        case Format::R32_SFLOAT:
            color.r = *reinterpret_cast<const float*>(ptr);
            break;
        }
        return color;
    }

    void write_pixel(glm::uvec2 p_pos, const glm::vec4& p_data, uint32_t p_mip_level) noexcept {
        // TODO
    }

    void to_swizzle(uint32_t p_mip_level) noexcept {
        // TODO
    }

    void to_linear(uint32_t p_mip_level) noexcept {
        // TODO
    }

private:
    static constexpr uint32_t BYTE_PER_PIXEL = 4;

    struct MipInfo {
        std::span<uint8_t> memory;
        glm::uvec2 extent;
        uint32_t row_pitch;
        bool swizzle;
    };

    static size_t get_offset(const glm::uvec2& p_pos, const MipInfo& p_info) {
        return p_info.swizzle ? morton_encode(p_pos.x, p_pos.y) * BYTE_PER_PIXEL : p_pos.y * p_info.row_pitch + p_pos.x * BYTE_PER_PIXEL;
    }

    Format format;
    uint32_t mipmap_lod;
    Vector<uint8_t, RenderAllocator> buffer;
    Vector<MipInfo, RenderAllocator> mip_infos;
};

} // namespace WhiteBirdEngine

#endif

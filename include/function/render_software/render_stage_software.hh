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
#ifndef WBE_FILE_RENDER_STAGE_SOFTWARE
#define WBE_FILE_RENDER_STAGE_SOFTWARE

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/render_defs.hh"
#include "function/render_software/render_software_defs.hh"
#include "function/render_software/ro_framebuffer_software.hh"
#include "function/render_software/ro_image_software.hh"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_uint2.hpp"
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <span>
namespace WhiteBirdEngine {

struct VertexDataSoftware {
    VertexDataSoftware(RenderAllocator* p_allocator) : layout(p_allocator) {
    }
    struct ElementDescription {
        enum class Type {
            FLOAT32,
            INT8,
        };
        Type type{};
        uint32_t element_offset = 0;
        uint32_t element_stride = 0;
        uint32_t element_count = 0;
    };

    std::span<uint8_t> buffer;
    Vector<ElementDescription, RenderAllocator> layout;
    uint32_t vertex_stride = 0;
    uint32_t element_count = 0;
};

struct FragmentSoftware {
    glm::uvec2 coordinate;
    float depth;
    glm::vec3 barycentric;
};

struct RenderPerThreadScreenSpaceJobSoftware {};

struct RenderContextSoftware {
    RenderContextSoftware(BaseRenderDataSoftware* p_render_data)
        : base_render_data(p_render_data), input_buffer(p_render_data->main_allocator),
          vertex_in(p_render_data->main_allocator), vertex_out(p_render_data->main_allocator) {
    }
    BaseRenderDataSoftware* base_render_data;

    Vector<uint8_t, RenderAllocator> input_buffer;

    VertexDataSoftware vertex_in;
    std::span<uint32_t> indeices;
    VertexDataSoftware vertex_out;
    std::span<FragmentSoftware> fragments;

    Ref<ROImageSoftware> depth;
    Ref<ROImageSoftware> color;
};

class RenderStagesSoftware final {
public:
    using RenderStageType = void (*)(RenderContextSoftware*);

    struct Builder {
        Builder& set_vertex_shading_stage(RenderStageType p_stage) {
            vertex_shading_stage = p_stage;
            return *this;
        }
        Builder& set_rasterization_stage(RenderStageType p_stage) {
            rasterization_stage = p_stage;
            return *this;
        }
        Builder& set_fragment_shading_stage(RenderStageType p_stage) {
            fragment_shading_stage = p_stage;
            return *this;
        }
        Ref<RenderStagesSoftware> build_ref(BaseRenderDataSoftware* p_render_data) const {
            return make_ref<RenderStagesSoftware>(
                p_render_data->main_allocator, vertex_shading_stage, rasterization_stage, fragment_shading_stage);
        }
        RenderStageType vertex_shading_stage = default_vertex_shading_stage;
        RenderStageType rasterization_stage = default_rasterization_stage;
        RenderStageType fragment_shading_stage = default_fragment_shading_stage;
    };

    RenderStagesSoftware();
    ~RenderStagesSoftware() = default;
    RenderStagesSoftware(const RenderStagesSoftware&) = delete;
    RenderStagesSoftware(RenderStagesSoftware&&) = delete;
    RenderStagesSoftware& operator=(const RenderStagesSoftware&) = delete;
    RenderStagesSoftware& operator=(RenderStagesSoftware&&) = delete;

    RenderStagesSoftware(RenderStageType p_vertex_shading_stage, RenderStageType p_rasterization_stage, RenderStageType p_fragment_shading_stage);

    Vector<RenderStageType, RenderAllocator> get_graphics_pipeline(BaseRenderDataSoftware* p_render_context) const {
        return Vector<RenderStageType, RenderAllocator>(
            {vertex_shading_stage, rasterization_stage, fragment_shading_stage}, p_render_context->main_allocator);
    }

private:
    static void default_vertex_shading_stage(RenderContextSoftware* p_render_context) noexcept;
    static void default_rasterization_stage(RenderContextSoftware* p_render_context) noexcept;
    static void default_fragment_shading_stage(RenderContextSoftware* p_render_context) noexcept;

    RenderStageType vertex_shading_stage;
    RenderStageType rasterization_stage;
    RenderStageType fragment_shading_stage;
};

} // namespace WhiteBirdEngine

#endif

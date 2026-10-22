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
#include "function/render_software/render_stage_software.hh"

namespace WhiteBirdEngine {

RenderStagesSoftware::RenderStagesSoftware()
    : vertex_shading_stage(default_vertex_shading_stage), rasterization_stage(default_rasterization_stage),
      fragment_shading_stage(default_fragment_shading_stage) {
}

RenderStagesSoftware::RenderStagesSoftware(
    RenderStageType p_vertex_shading_stage, RenderStageType p_rasterization_stage, RenderStageType p_fragment_shading_stage)
    : vertex_shading_stage(p_vertex_shading_stage), rasterization_stage(p_rasterization_stage),
      fragment_shading_stage(p_fragment_shading_stage) {
}

void RenderStagesSoftware::default_vertex_shading_stage(RenderContextSoftware* p_render_context) noexcept {
    // TODO
}

void RenderStagesSoftware::default_rasterization_stage(RenderContextSoftware* p_render_context) noexcept {
    // TODO
}

void RenderStagesSoftware::default_fragment_shading_stage(RenderContextSoftware* p_render_context) noexcept {
    // TODO
}

} // namespace WhiteBirdEngine

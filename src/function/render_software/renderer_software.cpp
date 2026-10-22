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
#include "function/render_software/renderer_software.hh"
#include "core/allocator/i_allocator.hh"
#include "core/job/job_handler.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include <cstddef>
#include <functional>
#include <future>
#include <stdexcept>
#include <string_view>

namespace WhiteBirdEngine {

RendererSoftware::RendererSoftware(Ref<RenderAllocator> p_allocator, RendererCapabilities& p_capabilities)
    : render_allocator(p_allocator), per_thread_allocators(render_allocator.get()), render_stages(render_allocator.get()),
      base_render_data(render_allocator.get()) {
    p_capabilities.conservative_rasterization = false;
    p_capabilities.ray_tracing = false;
    p_capabilities.ray_query = false;
    p_capabilities.timestamp_query = false;
    p_capabilities.descriptor_set = false;
}

void RendererSoftware::tick(double p_delta) {
    (void)p_delta;
    // TODO
}
Ref<RenderDatabase> RendererSoftware::get_render_database() {
    return MEM_NULL; // TODO
}
void RendererSoftware::register_render_graph(std::string_view p_resource_key) {
    (void)p_resource_key;
    // TODO
    throw std::runtime_error("Not implemented.");
}
void RendererSoftware::unregister_render_graph(std::string_view p_resource_key) {
    (void)p_resource_key;
    // TODO
    throw std::runtime_error("Not implemented.");
}
void RendererSoftware::activate_render_graph(std::string_view p_resource_key) {
    (void)p_resource_key;
    // TODO
    throw std::runtime_error("Not implemented.");
}
std::future<void> RendererSoftware::add_job(std::function<void()> p_job) {
    (void)p_job;
    // TODO
    return create_completed_job_future();
}
} // namespace WhiteBirdEngine

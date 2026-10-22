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
#ifndef WBE_FILE_RENDERER_DUMMY_H
#define WBE_FILE_RENDERER_DUMMY_H
#include "core/job/job_handler.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
namespace WhiteBirdEngine {

/**
 * @class RendererDummy
 * @brief A dummy renderer that omits rendering and does not support render graphs.
 *
 */
class RendererDummy final : public IRenderer {
public:
    RendererDummy(RenderAllocator* p_allocator, RendererCapabilities& p_capabilities)
        : dummy_database(make_ref<RenderDatabase>(p_allocator, p_allocator)) {
        p_capabilities.conservative_rasterization = false;
        p_capabilities.ray_tracing = false;
        p_capabilities.ray_query = false;
        p_capabilities.timestamp_query = false;
        p_capabilities.descriptor_set = false;
    }

    virtual ~RendererDummy() override = default;
    RendererDummy(const RendererDummy&) = delete;
    RendererDummy(RendererDummy&&) noexcept = delete;
    RendererDummy& operator=(const RendererDummy&) = delete;
    RendererDummy& operator=(RendererDummy&&) noexcept = delete;

    virtual uint32_t get_worker_nums() override {
        return 1;
    }

    virtual void tick(double p_delta) override {
        (void)p_delta;
    }

    virtual Ref<RenderDatabase> get_render_database() override {
        // Because of RAII, we have to return a valid database since the caller
        // is going to assume this is valid.
        return dummy_database;
    }

    virtual void register_render_graph(std::string_view p_resource_key) override {
        throw std::runtime_error(std::string("RendererDummy does not support render graph registration: ") + std::string(p_resource_key));
    }

    virtual void unregister_render_graph(std::string_view p_resource_key) override {
        throw std::runtime_error(std::string("RendererDummy does not support render graph unregistration: ") + std::string(p_resource_key));
    }

    virtual void activate_render_graph(std::string_view p_resource_key) override {
        throw std::runtime_error(std::string("RendererDummy does not support render graph activation: ") + std::string(p_resource_key));
    }

    virtual std::future<void> add_job(std::function<void()> p_job) override {
        (void)p_job;
        return create_completed_job_future();
    }

private:
    Ref<RenderDatabase> dummy_database;
};

} // namespace WhiteBirdEngine

#endif

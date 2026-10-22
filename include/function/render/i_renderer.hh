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
#ifndef WBE_FILE_RENDERER_HH
#define WBE_FILE_RENDERER_HH

#include "core/job/job_handler.hh"
#include "core/memory/reference_strong.hh"
#include "core/reflection/reflection_defs.hh"
#include "function/render/render_database.hh"
#include "utils/interface/i_singleton.hh"
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <string_view>
namespace WhiteBirdEngine {

WBE_LABEL(WBE_CHANNEL_RENDER, WBE_CHANNEL);

/**
 * @struct RendererCapabilities
 * @brief Optional renderer capabilities requested by the caller and enabled by the renderer.
 */
struct RendererCapabilities {
    /**
     * @brief Whether GPU shader reports are retrieved, logged, and treated as fatal errors.
     */
    bool shader_debug = false;
    /**
     * @brief Whether conservative rasterization is requested and was enabled.
     */
    bool conservative_rasterization = false;
    /**
     * @brief Whether ray tracing is requested and was enabled.
     */
    bool ray_tracing = false;
    /**
     * @brief Whether ray query is requested and was enabled.
     */
    bool ray_query = false;
    /**
     * @brief Whether GPU timestamp queries are requested and were enabled.
     *
     * When enabled, the renderer samples the GPU execution time of every render-graph task and writes a JSON report when
     * it terminates.
     */
    bool timestamp_query = false;
    /**
     * @brief Whether classic Vulkan descriptor sets are requested and were enabled.
     *
     * When enabled, the renderer provides a descriptor pool for code that cannot use the renderer's descriptor heaps, such as
     * third-party libraries.
     */
    bool descriptor_set = false;
};

/**
 * @class IRenderer
 * @brief IRenderer interface for different types of renderers, usually refer
 * to the rendering API.
 *
 */
class IRenderer : public ISingleton<IRenderer>, public JobHandler {
public:
    IRenderer() = default;
    virtual ~IRenderer() override = default;
    IRenderer(const IRenderer&) = delete;
    IRenderer(IRenderer&&) noexcept = delete;
    IRenderer& operator=(const IRenderer&) = delete;
    IRenderer& operator=(IRenderer&&) noexcept = delete;

    /**
     * @brief Called every frame.
     *
     * @param p_delta The difference between the last two calls of this
     * function.
     */
    virtual void tick(double p_delta) = 0;

    /**
     * @brief Request a PNG capture of the next rendered frame before presentation.
     * @details Call on the rendering thread; other threads must submit the request through add_job().
     * The renderer copies the path and completes the capture during a subsequent tick that renders a frame.
     * Readback may stall rendering. Unsupported backends, invalid paths, pending requests, and capture failures throw.
     * @param p_path Destination PNG file path; its parent directory must already exist.
     */
    virtual void request_frame_capture(std::string_view p_path);

    /**
     * @brief Submit a job like add_job(), unless the renderer has stopped accepting jobs.
     *
     * Use this from threads that may still submit work while the renderer terminates. The default implementation never
     * refuses and forwards to add_job().
     *
     * @param p_job The job to run on the rendering thread.
     * @return The future completed after the job runs, or std::nullopt when the renderer refused the job; a refused job
     * never runs.
     */
    virtual std::optional<std::future<void>> try_add_job(std::function<void()> p_job);

    /**
     * @brief Stop accepting jobs and run every job still queued.
     *
     * Afterwards add_job() throws and try_add_job() refuses. Call it after the rendering thread has stopped, while the
     * objects that queued jobs capture are still alive, such as at the end of an application's run loop. The default
     * implementation does nothing, for renderers that do not queue jobs.
     */
    virtual void drain_job_buffer_tasks();

    /**
     * @brief Get number of workers working on rendering.
     *
     * @return The number of workers working on rendering.
     */
    virtual uint32_t get_worker_nums() = 0;

    /**
     * @brief Get the instance of the render database.
     *
     * @return The renderer's render database.
     */
    virtual Ref<RenderDatabase> get_render_database() = 0;

    /**
     * @brief Acquire a registered render graph by resource key.
     *
     * The concrete graph descriptor must already be available to the backend.
     * Repeated registrations acquire additional references to the same
     * compiled graph.
     *
     * @param p_resource_key The render graph resource key.
     */
    virtual void register_render_graph(std::string_view p_resource_key) = 0;

    /**
     * @brief Release a registered render graph by resource key.
     *
     * @param p_resource_key The render graph resource key.
     */
    virtual void unregister_render_graph(std::string_view p_resource_key) = 0;

    /**
     * @brief Activate a render graph.
     *
     * @param p_resource_key The resource key of the graph to activate.
     */
    virtual void activate_render_graph(std::string_view p_resource_key) = 0;
};

} // namespace WhiteBirdEngine
#endif

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
#ifndef WBE_FILE_RENDERER_VK_HH
#define WBE_FILE_RENDERER_VK_HH

#include "core/allocator/stl_allocator.hh"
#include "core/core_utils.hh"
#include "core/job/job_buffer_ring_mpsc.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/unique.hh"
#include "function/render/i_render_task.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/ktx_vulkan_device_info_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_graph_vk.hh"
#include "function/render_vk/render_plugin_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "function/render_vk/ro_fense_vk.hh"
#include "function/render_vk/ro_image_view_vk.hh"
#include "function/render_vk/ro_image_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/semaphore_vk.hh"
#include "function/render_vk/vma_vk.hh"
#include "function/window/i_window.hh"
#include "utils/defs.hh"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

static constexpr std::string_view REND_RES_SWAP_CHAIN = "swap_chain";

/**
 * @class RendererVK
 * @brief Vulkan renderer.
 *
 */
class RendererVK final : public IRenderer, public ThisRef<RendererVK> {
public:
    using AllocType = RenderAllocator;

    /**
     * @brief The version of Vulkan will be used for the renderer.
     */
    static constexpr uint32_t VULKAN_API_VERSION = VK_API_VERSION_1_3;

    /**
     * @brief The number of jobs that would be runned per frame.
     */
    static constexpr size_t RUN_MAX_JOBS_PER_FRAME = std::numeric_limits<size_t>::max();
    static constexpr size_t MAX_DELAYED_CALLS = WBE_KI_B(5);

    virtual ~RendererVK() override;
    RendererVK(const RendererVK&) = delete;
    RendererVK(RendererVK&&) = delete;
    RendererVK& operator=(const RendererVK&) = delete;
    RendererVK& operator=(RendererVK&&) = delete;

    /**
     * @brief Constructor.
     *
     * @param p_allocator The allocator that the render resources will be
     * allocated on. This will also be the allocator for Vulkan.
     * @param p_window The window whose surface and swap chain are rendered.
     * @param p_job_buffer The render job buffer. Used for committing jobs for
     * the renderer to perform.
     * @param p_capabilities The requested capabilities. Unsupported requested capabilities are disabled during construction.
     */
    RendererVK(AllocType* p_allocator, Ref<IWindow> p_window, Ref<JobBufferRingMPSC> p_job_buffer, RendererCapabilities& p_capabilities);

    virtual uint32_t get_worker_nums() override {
        return 1;
    }

    virtual void tick(double p_delta) override;

    virtual void request_frame_capture(std::string_view p_path) override;

    virtual void register_render_graph(std::string_view p_resource_key) override;

    virtual void unregister_render_graph(std::string_view p_resource_key) override;

    virtual void activate_render_graph(std::string_view p_resource_key) override;

    virtual Ref<RenderDatabase> get_render_database() override {
        return render_database;
    }


    /**
     * @brief Register one-shot Vulkan work to be recorded and submitted before graph execution.
     *
     * Registered jobs are batched into the renderer's reusable graphics command buffer. The renderer retains captured resources
     * until completion and synchronizes the batch with later compute-queue work without blocking the rendering thread.
     *
     * @param p_job The GPU job to record.
     * @return A future that becomes ready after queue submission and contains the RAII fence that signals after GPU completion.
     */
    std::future<Ref<ROFenseVK>> register_gpu_job(GPUJobVK p_job);

    /**
     * @brief Submit work from another thread for execution on the rendering thread.
     *
     * Render objects are render-thread-confined. Cross-thread callers must use this job buffer instead of operating on them
     * directly. drain_job_buffer_tasks(), which termination also calls, stops accepting jobs and runs the queued ones;
     * threads that may submit work while the jobs are drained use try_add_job().
     *
     * @param p_job Work to execute on the rendering thread.
     * @return Future completed after the job runs.
     * @throws std::runtime_error If the renderer is terminating and refuses the job.
     */
    virtual std::future<void> add_job(std::function<void()> p_job) override;

    /**
     * @brief Submit work for execution on the rendering thread unless the renderer is terminating.
     *
     * Every accepted job runs, at the latest during termination. Safe to call from any thread.
     */
    virtual std::optional<std::future<void>> try_add_job(std::function<void()> p_job) override;

    /**
     * @brief Wait for the device to become idle, stop accepting jobs, and run every queued job.
     *
     * Producers already submitting finish their submissions, and their jobs run too. Termination calls this again in case
     * the owner did not.
     */
    virtual void drain_job_buffer_tasks() override;

    /**
     * @brief Register a Vulkan render graph that a callback builds, under a resource key.
     *
     * The graph calls p_rebuild for its first build and again whenever it rebuilds, such as when the swap chain is
     * resized; see RenderGraphVK::build_from_callback(). Call on the rendering thread.
     *
     * @param p_resource_key The resource key to register under.
     * @param p_rebuild Produces the graph's descriptor for a context; everything it captures must outlive the graph.
     * @return The newly built render graph.
     * @throws std::runtime_error If the key is already registered, or the callback's descriptor does not build.
     */
    Ref<RenderGraphVK> register_render_graph(std::string_view p_resource_key, RendGraphRebuildCallbackVK p_rebuild);

    /**
     * @brief Run one renderer frame and, when a graph is given, render it to the swap chain.
     *
     * Advances to the next frame-in-flight slot and waits for that slot's fence, then runs queued render jobs, due delayed
     * calls, the shader-debug readback, and pending GPU jobs. When p_graph is not null, it acquires a swap-chain image,
     * executes the graph, presents, and waits for the frame's fence. Must be called on the rendering thread.
     *
     * @param p_graph The graph to execute, or null to only run the frame's renderer work.
     * @param p_delta The frame delta forwarded to graph tasks.
     */
    void execute_graph(Ref<RenderGraphVK> p_graph, double p_delta);

    /**
     * @brief Get the singleton instance.
     *
     * @return The singleton instance.
     */
    static RendererVK* get_singleton() {
        return singleton;
    }

    /**
     * @brief Get the stable resource creation context owned by this renderer.
     *
     * @return The non-owning context pointer. Resource operations run on the rendering thread.
     */
    const RenderContextVK* get_render_context() const {
        return &render_context;
    }

    /**
     * @brief Acquire shared ownership of the per-frame swap-chain image-view container.
     *
     * Render graphs bind this stable container explicitly under `REND_RES_SWAP_CHAIN`. The renderer updates the current
     * frame slot after acquiring a swap-chain image.
     *
     * @return The renderer-owned swap-chain image-view container.
     * @throws std::runtime_error If the renderer has no swap-chain image-view container.
     */
    Ref<ROCMultislotsVK> acquire_swap_chain_image_view_container() const;

    /**
     * @brief Register a task to be called after a number of frames. This could
     * be used for resource deallocation to prevent destroying resources that
     * are currently in-flight. Note that when the renderer terminates, all
     * delayed calls will be called together at once.
     *
     * @param p_task The task to be called.
     * @param p_delayed_frames The number of frames to be delayed.
     */
    void register_delay_call(std::function<void()>&& p_task, uint32_t p_delayed_frames) {
        for (auto& delayed_call : delayed_calls) {
            if (delayed_call.second < 0) {
                delayed_call.first = std::move(p_task);
                delayed_call.second = static_cast<int32_t>(p_delayed_frames);
                return;
            }
        }
        throw std::runtime_error("Failed to register delay call: buffer overflow.");
    }

private:
    Ref<JobBufferRingMPSC> job_buffer;
    // Cleared by drain_job_buffer_tasks(); try_add_job() refuses jobs afterwards.
    std::atomic<bool> accepting_jobs = true;
    // Producers between checking accepting_jobs and finishing their push; draining continues until none remain.
    std::atomic<uint32_t> submitting_job_count = 0;

    inline static RendererVK* singleton = nullptr;

    AllocType* allocator = nullptr;
    Ref<RenderDatabase> render_database;

    Ref<IWindow> window;

    // Vulkan properties
    VkInstance vk_instance = VK_NULL_HANDLE;
    // Whether VK_EXT_debug_utils is enabled on the instance, so Vulkan objects can be named.
    bool debug_utils_enabled = false;

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice logical_device = VK_NULL_HANDLE;
    VmaAllocator vma_allocator = VK_NULL_HANDLE;
    Unique<KTXVulkanDeviceInfoVK> ktx_vulkan_device_info;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swap_chain = VK_NULL_HANDLE;
    uint32_t swap_chain_images_count = 0;
    uint32_t frames_in_flight = 0;
    // Pending readback request; accessed only by the rendering thread.
    String<AllocType> capture_png_path;
    bool frame_capture_supported = false;

    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue compute_queue_1 = VK_NULL_HANDLE;
    VkQueue compute_queue_2 = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    // Queue indices within their queue families, assigned when the logical device is created.
    uint32_t graphics_queue_index = 0;
    uint32_t compute_queue_1_index = 0;
    uint32_t compute_queue_2_index = 0;

    VkFormat swap_chain_image_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swap_chain_extent{};

    VkViewport viewport{};
    VkRect2D scissor{};

    VkCommandPool graphics_command_pool = VK_NULL_HANDLE;
    VkCommandPool compute_command_pool = VK_NULL_HANDLE;

    Unique<DescriptorHeapAllocatorVK> descriptor_heap_allocator;
    Ref<ROCMultislotsVK> swap_chain_image_view_container;
    Ref<ROBufferVK> shader_debug_buffer;

    Vector<Ref<ROFenseVK>, AllocType> in_flight_fences;
    std::array<std::pair<std::function<void()>, int32_t>, MAX_DELAYED_CALLS> delayed_calls;

    // Per-frame-in-flight binary semaphore signaled by vkAcquireNextImageKHR.
    Vector<Ref<SemaphoreVK>, AllocType> image_available_semaphores;

    struct RegisteredRenderGraph {
        Ref<RenderGraphVK> graph;
        uint32_t reference_count = 0;
    };

    // Registered compiled render graphs, keyed by resource key.
    Map<std::string, RegisteredRenderGraph, AllocType> render_graphs;
    // The currently active cached render graph.
    Ref<RenderGraphVK> active_graph;

    std::vector<const char*> enabled_layers{
#ifdef WBE_VK_ENABLE_VALIDATION
        "VK_LAYER_KHRONOS_validation"
#endif
    };

    const std::vector<const char*> enabled_instance_extensions;

    std::vector<const char*> enabled_device_extensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
        VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_5_EXTENSION_NAME,

        // To make VK_IMAGE_LAYOUT_GENERAL works as well as other layouts.
        VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME,
    };

    struct QueueFamilyIndices {
        AllocType* allocator = nullptr;
        std::optional<uint32_t> graphics; // For basic pipeline rendering
        std::optional<uint32_t> compute;  // For compute shader
        std::optional<uint32_t> present;  // For presenting contents in window
        bool is_complete() const {
            return graphics.has_value() && compute.has_value() && present.has_value();
        }
        uint32_t get_graphics() const {
            return get_required_index(graphics, "graphics");
        }
        uint32_t get_compute() const {
            return get_required_index(compute, "compute");
        }
        uint32_t get_present() const {
            return get_required_index(present, "present");
        }
        Set<uint32_t, AllocType> get_unique_indices() const {
            WBE_DEBUG_ASSERT(is_complete());
            Set<uint32_t, AllocType> result(allocator);
            result.insert(get_graphics());
            result.insert(get_compute());
            result.insert(get_present());
            return result;
        }

    private:
        static uint32_t get_required_index(const std::optional<uint32_t>& p_index, const char* p_name) {
            if (!p_index.has_value()) {
                throw std::runtime_error(std::string("RendererVK: missing ") + p_name + " queue family index.");
            }
            return *p_index;
        }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        Vector<VkSurfaceFormatKHR, AllocType> formats;
        Vector<VkPresentModeKHR, AllocType> present_modes;
    };

    RenderContextVK render_context;
    QueueFamilyIndices queue_family_indices;
    Vector<Ref<RenderPluginVK>, AllocType> registered_plugins;

    // Render objects for the renderer.
    struct RenderObjects {
        RenderObjects(AllocType* p_allocator) : swap_chain_images(p_allocator), swap_chain_image_views(p_allocator) {
        }
        ~RenderObjects() = default;
        RenderObjects(const RenderObjects&) = delete;
        RenderObjects(RenderObjects&&) noexcept = delete;
        RenderObjects& operator=(const RenderObjects&) = delete;
        RenderObjects& operator=(RenderObjects&&) noexcept = delete;
        // Swap chain images
        Vector<Ref<ROImageVK>, AllocType> swap_chain_images;
        // Swap chain image views.
        Vector<Ref<ROImageViewVK>, AllocType> swap_chain_image_views;
    };
    Unique<RenderObjects> render_objects;

    static constexpr size_t MAX_GPU_JOBS = 1024;
    VkCommandBuffer gpu_job_command_buffer = VK_NULL_HANDLE;
    struct RegisteredGPUJob {
        GPUJobVK record;
        Ref<ROFenseVK> completion_fense;
        std::promise<Ref<ROFenseVK>> submission_promise;
    };
    std::array<RegisteredGPUJob, MAX_GPU_JOBS> gpu_jobs;
    Vector<GPUJobVK, AllocType> running_gpu_jobs;
    Vector<Ref<ROFenseVK>, AllocType> running_gpu_job_fenses;
    // Every GPU-job batch signals the next value of this timeline semaphore. The compute queues wait on it in batches
    // without a fence, so it is kept until termination rather than replaced per batch, which would destroy it while those
    // waits may be pending.
    Ref<SemaphoreVK> gpu_job_semaphore;
    uint64_t gpu_job_semaphore_value = 0;

    void init(RendererCapabilities& p_capabilities);
    void terminate();
    void register_requested_plugins(const RendererCapabilities& p_capabilities);

    void init_vulkan(RendererCapabilities& p_capabilities);
    void setup_render_context();
    void init_swap_chain_image_views();
    void recreate_swap_chain();

    void create_instance();
    void create_device(RendererCapabilities& p_capabilities);
    void create_vma_allocator();
    void create_swap_chain();
    void clean_up_swap_chain();
    void create_command_pools();
    void create_ktx_vulkan_device_info();
    void create_gpu_job_command_buffer();
    void refresh_swap_chain_image_view_container();
    void create_descriptor_heap_allocator();
    void create_shader_debug_buffer();
    void create_async_objects();

    bool check_layer_support();
    void pick_physical_device(const Vector<VkPhysicalDevice, AllocType>& p_physical_devices);
    void create_physical_device();
    void create_logical_device();
    void create_swap_chain_images();

    bool is_device_suitable(VkPhysicalDevice p_device);
    std::vector<std::string> collect_device_unsuitability_reasons(VkPhysicalDevice p_device);
    void log_device_unsuitability(VkPhysicalDevice p_device, std::string_view p_context);

    QueueFamilyIndices get_queue_family_indices(VkPhysicalDevice p_device);
    SwapChainSupportDetails get_swap_chain_support_details(VkPhysicalDevice p_device);
    VkSurfaceFormatKHR choose_swap_surface_format(const Vector<VkSurfaceFormatKHR, AllocType>& p_available_formats);
    VkPresentModeKHR choose_swap_present_mode(const Vector<VkPresentModeKHR, AllocType>& p_available_present_modes);
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& p_capabilities);

    VkAllocationCallbacks* get_allocation_callbacks() {
        return nullptr;
    }

    void run_stored_jobs();
    std::optional<uint32_t> acquire_swapchain_next_image(VkSemaphore p_image_avialable_semaphore);
    uint32_t advance_frame_slot();
    void run_delayed_calls();
    void run_gpu_jobs();
    void check_shader_debug_buffer();
    void synchronize_gpu_and_flush_delayed_calls();
    void flush_delayed_calls();
    void wait_for_running_gpu_jobs();
    void submit_gpu_job_command_buffer(const Vector<Ref<ROFenseVK>, AllocType>& p_completion_fenses,
        std::vector<std::promise<Ref<ROFenseVK>>>& p_submission_promises,
        uint64_t p_completion_value);
    void present(uint32_t p_image_index, Ref<RenderGraphVK> p_graph);
    void capture_swap_chain_image(uint32_t p_image_index);
};

} // namespace WhiteBirdEngine

#endif

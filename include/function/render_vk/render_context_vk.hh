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
#ifndef WBE_FILE_RENDER_CONTEXT_VK_HH
#define WBE_FILE_RENDER_CONTEXT_VK_HH

#include "function/render_vk/render_vk_defs.hh"

namespace WhiteBirdEngine {

class ROBufferVK;
class ROImageVK;
class ROImageViewVK;
class ROSamplerVK;
class ROStagingBufferVK;
class ROShaderVK;
class ROGraphicsPipelineVK;
class ROComputePipelineVK;
struct BufferDescVK;
struct BufferAliasDescVK;
struct ImageDescVK;
struct ImageAliasDescVK;
struct ImageViewDescVK;
struct SamplerDescVK;
struct StagingBufferDescVK;
struct ShaderDescVK;
struct GraphicsPipelineDescVK;
struct ComputePipelineDescVK;

/**
 * @class RenderContextVK
 * @brief Renderer-owned state and resource creation facilities.
 *
 * Non-copyable and non-movable: resources and delayed callbacks retain this context's stable address. The renderer
 * updates its fields in place during swap-chain recreation. The context does not own the Vulkan handles exposed here.
 * All creation methods run synchronously on the rendering thread. Other threads submit work through IRenderer::add_job().
 * Created resources and their deferred destruction must finish before renderer shutdown; the allocator must outlive them.
 * Include the corresponding resource header when using a factory result or its description.
 */
class RenderContextVK {
public:
    RenderContextVK() = delete;
    ~RenderContextVK() = default;
    RenderContextVK(const RenderContextVK&) = delete;
    RenderContextVK(RenderContextVK&&) = delete;
    RenderContextVK& operator=(const RenderContextVK&) = delete;
    RenderContextVK& operator=(RenderContextVK&&) = delete;

    /**
     * @brief Construct a context over a renderer-owned allocator.
     * @param p_allocator Allocator that outlives the context and every resource it creates.
     * @throws std::runtime_error If the allocator is null.
     */
    explicit RenderContextVK(RenderAllocator* p_allocator);

    /**
     * @brief Create a shared buffer using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROBufferVK> create_buffer(const BufferDescVK& p_description) const;

    /**
     * @brief Create a shared buffer using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROBufferVK> create_buffer(const BufferAliasDescVK& p_description) const;

    /**
     * @brief Create a shared image using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROImageVK> create_image(const ImageDescVK& p_description) const;

    /**
     * @brief Create a shared image using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROImageVK> create_image(const ImageAliasDescVK& p_description) const;

    /**
     * @brief Create a shared image view using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROImageViewVK> create_image_view(const ImageViewDescVK& p_description) const;

    /**
     * @brief Create a shared sampler using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROSamplerVK> create_sampler(const SamplerDescVK& p_description) const;

    /**
     * @brief Create a shared staging buffer using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROStagingBufferVK> create_staging_buffer(const StagingBufferDescVK& p_description) const;

    /**
     * @brief Create a shared shader using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROShaderVK> create_shader(const ShaderDescVK& p_description) const;

    /**
     * @brief Create a shared graphics pipeline using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROGraphicsPipelineVK> create_graphics_pipeline(const GraphicsPipelineDescVK& p_description) const;

    /**
     * @brief Create a shared compute pipeline using this context's allocator and the existing resource construction contract.
     * @param p_description Complete construction description; resource ownership transfers only after successful creation.
     * @return A copyable owning reference. The context and its allocator must outlive the resource.
     * @throws std::runtime_error If the description is invalid or resource creation fails.
     */
    Ref<ROComputePipelineVK> create_compute_pipeline(const ComputePipelineDescVK& p_description) const;

    /**
     * @brief Non-owning render allocator used by resource factories.
     */
    RenderAllocator* allocator = nullptr;
    // The render database.
    RenderDatabase* render_database = nullptr;
    /** Capabilities negotiated when the renderer was constructed. */
    RendererCapabilities renderer_capabilities{};

    // The logic device.
    VkDevice logic_device = VK_NULL_HANDLE;
    // The physical device that owns the renderer queues and resources.
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    /** Command for setting dynamic vertex input state. */
    PFN_vkCmdSetVertexInputEXT command_set_vertex_input = nullptr;
    /** Command for naming Vulkan objects (VK_EXT_debug_utils); null when the extension is unavailable. */
    PFN_vkSetDebugUtilsObjectNameEXT set_debug_utils_object_name = nullptr;
    /** The VMA allocator used for owned Vulkan buffers and images. */
    VmaAllocator vma_allocator = VK_NULL_HANDLE;
    /** The image format of the swap chain. */
    VkFormat swap_chain_format = VK_FORMAT_UNDEFINED;
    /** The size of the swap chain. */
    VkExtent2D swap_chain_extent{};
    /** The aspect ratio of the swap chain. */
    float swap_chain_aspect_ratio = 1.0F;
    /** Callback for allocator. */
    VkAllocationCallbacks* allocation_callbacks = nullptr;
    /** Graphics queue family. */
    VkQueue graphics_queue = VK_NULL_HANDLE;
    /** First compute queue, used by QueueFamilyVK::COMPUTE_1. It may equal graphics_queue when no other queue is available. */
    VkQueue compute_queue_1 = VK_NULL_HANDLE;
    /** Second compute queue, used by QueueFamilyVK::COMPUTE_2. It may equal another queue when no other queue is available. */
    VkQueue compute_queue_2 = VK_NULL_HANDLE;
    /** The queue family index of graphics_queue. */
    uint32_t graphics_queue_family_index = UINT32_MAX;
    /** The viewport for the final presentation. */
    VkViewport viewport{};
    /** The scissor for the final presentation. */
    VkRect2D scissor{};

    /** Graphics command pool. */
    VkCommandPool graphics_command_pool = VK_NULL_HANDLE;
    /** Compute command pool shared by both compute queues. */
    VkCommandPool compute_command_pool = VK_NULL_HANDLE;
    /** Non-owning libktx Vulkan upload context owned by RendererVK. */
    KTXVulkanDeviceInfoVK* ktx_vulkan_device_info = nullptr;
    /** Descriptor-heap properties reported by the physical device. */
    VkPhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
    };
    /** Renderer-owned resource descriptor heap. */
    DescriptorHeapIDVK resource_descriptor_heap_id = UINT32_MAX;
    /** Renderer-owned sampler descriptor heap. */
    DescriptorHeapIDVK sampler_descriptor_heap_id = UINT32_MAX;
    /** Renderer-owned buffer descriptor arena. */
    DescriptorArenaLocationVK buffer_descriptor_arena{};
    /** Renderer-owned sampled-image descriptor arena. */
    DescriptorArenaLocationVK sampled_image_descriptor_arena{};
    /** Number of slots in the sampled-image descriptor arena. */
    uint32_t sampled_image_descriptor_capacity = 0;
    /** Renderer-owned storage-image descriptor arena. */
    DescriptorArenaLocationVK storage_image_descriptor_arena{};
    /** Renderer-owned sampler descriptor arena. */
    DescriptorArenaLocationVK sampler_descriptor_arena{};
    /** Stride used by sampler descriptors. */
    VkDeviceSize sampler_descriptor_stride = 0;
    /** Non-owning renderer descriptor-heap allocator. */
    DescriptorHeapAllocatorVK* descriptor_heap_allocator = nullptr;
    /** Host command for encoding resource descriptors. */
    PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors = nullptr;
    /** Host command for encoding sampler descriptors. */
    PFN_vkWriteSamplerDescriptorsEXT write_sampler_descriptors = nullptr;
    /** Command for binding the resource descriptor heap. */
    PFN_vkCmdBindResourceHeapEXT command_bind_resource_heap = nullptr;
    /** Command for binding the sampler descriptor heap. */
    PFN_vkCmdBindSamplerHeapEXT command_bind_sampler_heap = nullptr;
    /** Command for uploading descriptor indices and other pipeline push data. */
    PFN_vkCmdPushDataEXT command_push_data = nullptr;
    /** Maximum indexed-indirect draws accepted by one Vulkan draw call. */
    uint32_t max_draw_indirect_count = 1;
    /** The fence that blocks every render tick. */
    VkFence in_flight_fence = VK_NULL_HANDLE;
    /** The current swap chain index. This is useful to trach which frame in
     * flight it is right now. */
    uint32_t current_swap_chain_index = 0;
    /** The number of images owned by the current swap chain. */
    uint32_t swap_chain_images_count = 0;
    /** The current frame-in-flight slot (matches `in_flight_fence` slot index). */
    uint32_t current_frame_slot = 0;
    /** Frames in flight. */
    uint32_t frames_in_flight = 0;

    /** The current number of frames. */
    uint64_t current_frame = 0;
    /** Elapsed renderer update time in seconds. */
    double time = 0.0;

    /** The queue family indices. */
    Set<uint32_t, RenderAllocator> queue_family_indices;

    /** Non-owning list of the renderer's enabled plugins, or null when no renderer owns this data. */
    Vector<Ref<RenderPluginVK>, RenderAllocator>* render_plugins = nullptr;

    /** Function to register a delayed call. */
    std::function<void(std::function<void()>&& p_function, uint32_t p_delayed_frames)> register_delayed_call;
};


} // namespace WhiteBirdEngine

#endif

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
#ifndef WBE_FILE_RENDER_VK_DEFS_HH
#define WBE_FILE_RENDER_VK_DEFS_HH

#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_renderer.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/ktx_vulkan_device_info_vk.hh"
#include "function/render_vk/vma_vk.hh"

#include <cstdint>
#include <functional>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

class DescriptorHeapAllocatorVK;
class RenderPluginVK;

/** @brief Identifies a physical Vulkan descriptor heap owned by the renderer. */
using DescriptorHeapIDVK = uint32_t;
/** @brief Identifies an arena within a physical Vulkan descriptor heap. */
using DescriptorArenaIDVK = uint32_t;

/**
 * @struct DescriptorArenaLocationVK
 * @brief Identifies an arena in a physical descriptor heap.
 */
struct DescriptorArenaLocationVK {
    /** Physical descriptor heap identifier. */
    DescriptorHeapIDVK heap_id = UINT32_MAX;
    /** Arena identifier local to that heap. */
    DescriptorArenaIDVK arena_id = UINT32_MAX;
};

/** One-shot Vulkan command-buffer recording job. */
using GPUJobVK = std::function<void(VkCommandBuffer)>;

/**
 * @class BaseRayTracingDataVK
 * @brief Vulkan procedures, limits, and shared handles used by ray-tracing render objects.
 */
struct BaseRayTracingDataVK {
    /**
     * @brief Device-reported size of an acceleration-structure descriptor in the resource heap.
     */
    VkDeviceSize acceleration_structure_descriptor_size = 0;
    /**
     * @brief Arena containing acceleration-structure descriptors.
     */
    DescriptorArenaLocationVK acceleration_structure_descriptor_arena{};
    /**
     * @brief Device command for creating an acceleration structure.
     */
    PFN_vkCreateAccelerationStructureKHR create_acceleration_structure = nullptr;
    /**
     * @brief Device command for destroying an acceleration structure.
     */
    PFN_vkDestroyAccelerationStructureKHR destroy_acceleration_structure = nullptr;
    /**
     * @brief Device command for querying acceleration-structure build sizes.
     */
    PFN_vkGetAccelerationStructureBuildSizesKHR get_acceleration_structure_build_sizes = nullptr;
    /**
     * @brief Device command for recording acceleration-structure builds.
     */
    PFN_vkCmdBuildAccelerationStructuresKHR command_build_acceleration_structures = nullptr;
    /**
     * @brief Device command for querying an acceleration-structure device address.
     */
    PFN_vkGetAccelerationStructureDeviceAddressKHR get_acceleration_structure_device_address = nullptr;
    /**
     * @brief Required device-address alignment for acceleration-structure scratch storage.
     */
    VkDeviceSize min_acceleration_structure_scratch_offset_alignment = 1;
    /**
     * @brief Maximum geometries accepted by one acceleration structure.
     */
    uint64_t max_acceleration_structure_geometry_count = 0;
    /**
     * @brief Maximum primitives accepted by one acceleration structure.
     */
    uint64_t max_acceleration_structure_primitive_count = 0;
};

class RenderContextVK;

/**
 * @brief The bindpoint for vertex buffer.
 */
constexpr uint32_t VERTEX_BUFFER_BINDPOINT = 0;

} // namespace WhiteBirdEngine

#endif

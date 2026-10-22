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
#include "function/render_vk/ro_acceleration_structure_vk.hh"
#include "function/render/render_database.hh"
#include "function/render_vk/debug_utils_vk.hh"
#include <string_view>
#include "core/core_utils.hh"
#include "function/function_utils.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/descriptor_heap_allocator_vk.hh"
#include "function/render_vk/i_ro_ray_tracing_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/ro_buffer_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <format>
#include <stdexcept>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

DebugData make_descriptor_row(uint32_t p_heap_index, VkDeviceAddress p_device_address) {
    DebugData row;
    row.set_value("resource_type", "ROAccelerationStructureVK");
    row.set_value("descriptor_type", "acceleration_structure");
    row.set_value("heap", "resource");
    row.set_value("heap_index", p_heap_index);
    row.set_value("device_address", std::format("0x{:x}", p_device_address));
    return row;
}

} // namespace

TypeID ROAccelerationStructureVK::get_type_uuid() const {
    return TypeUUID<ROAccelerationStructureVK>::UUID;
}

ROAccelerationStructureVK::ROAccelerationStructureVK(
    const RenderContextVK* p_render_data, const BaseRayTracingDataVK* p_ray_tracing_data, const Builder& p_builder)
    : IRORayTracingVK(p_render_data, p_ray_tracing_data),
      storage_buffer(p_render_data->create_buffer(ROBufferVK::Builder()
              .set_domain(DeviceDomain::DEVICE)
              .set_size(WBE_REQUIRED(p_builder.size))
              .set_usage(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))) {
    WBE_DEBUG_ASSERT(ray_tracing_data != nullptr);
    WBE_DEBUG_ASSERT(ray_tracing_data->create_acceleration_structure != nullptr);
    WBE_DEBUG_ASSERT(ray_tracing_data->destroy_acceleration_structure != nullptr);
    destroy_acceleration_structure = ray_tracing_data->destroy_acceleration_structure;
    VkAccelerationStructureCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create_info.buffer = storage_buffer->get_buffer();
    create_info.size = WBE_REQUIRED(p_builder.size);
    create_info.type = WBE_REQUIRED(p_builder.type);
    VkResult result = ray_tracing_data->create_acceleration_structure(
        render_data->logic_device, &create_info, render_data->allocation_callbacks, &acceleration_structure);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::format("Failed to create Vulkan acceleration structure: {}.", string_VkResult(result)));
    }
}

ROAccelerationStructureVK::~ROAccelerationStructureVK() {
    render_data->register_delayed_call(
        [render_data = render_data,
            acceleration_structure = acceleration_structure,
            destroy_acceleration_structure = destroy_acceleration_structure,
            descriptor_index = descriptor_index,
            descriptor_arena = ray_tracing_data->acceleration_structure_descriptor_arena]() {
            if (descriptor_index.has_value()) {
                render_data->descriptor_heap_allocator->deallocate_descriptor(
                    descriptor_arena.heap_id, descriptor_arena.arena_id, *descriptor_index);
            }
            destroy_acceleration_structure(render_data->logic_device, acceleration_structure, render_data->allocation_callbacks);
        },
        render_data->frames_in_flight);
}

VkDeviceAddress ROAccelerationStructureVK::get_device_address() const {
    VkAccelerationStructureDeviceAddressInfoKHR address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    address_info.accelerationStructure = acceleration_structure;
    return ray_tracing_data->get_acceleration_structure_device_address(render_data->logic_device, &address_info);
}

uint32_t ROAccelerationStructureVK::get_descriptor_index() const {
    if (!descriptor_index.has_value()) {
        DescriptorHeapAllocatorVK* allocator = render_data->descriptor_heap_allocator;
        if (allocator == nullptr) {
            throw std::runtime_error("Failed to create acceleration-structure descriptor: descriptor heap allocator is unavailable.");
        }
        const DescriptorArenaLocationVK arena = ray_tracing_data->acceleration_structure_descriptor_arena;
        descriptor_index = allocator->allocate_descriptor(arena.heap_id, arena.arena_id);
        VkDeviceAddressRangeEXT address_range{.address = get_device_address(), .size = 0};
        VkResourceDescriptorInfoEXT descriptor{};
        descriptor.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
        descriptor.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        descriptor.data.pAddressRange = &address_range;
        allocator->write_resource_descriptor(arena.heap_id, arena.arena_id, *descriptor_index, descriptor);
    }
    return *descriptor_index;
}

DebugDataRows ROAccelerationStructureVK::descriptor_debug_data() const {
    DebugDataRows rows(render_data->allocator);
    if (descriptor_index.has_value()) {
        rows.emplace_back(make_descriptor_row(*descriptor_index, get_device_address()));
    }
    return rows;
}

void ROAccelerationStructureVK::bind(RendGraphDescVK& p_graph, std::string_view p_resource_id) const {
    bind_render_object(p_graph, p_resource_id, acquire_this_ref());
}

void ROAccelerationStructureVK::apply_debug_name(std::string_view p_name) {
    set_debug_object_name_vk(render_data, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, acceleration_structure, p_name);
    if (storage_buffer != MEM_NULL && render_data->render_database != nullptr) {
        render_data->render_database->register_compiled_render_resource(std::format("{}.storage", p_name), storage_buffer);
    }
}

} // namespace WhiteBirdEngine

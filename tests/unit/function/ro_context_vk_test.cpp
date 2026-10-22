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
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/parser/parser_json.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/debug_data_vk.hh"
#include "function/render_vk/i_render_object_vk.hh"
#include "function/render_vk/render_context_vk.hh"
#include "function/render_vk/roc_multislots_vk.hh"
#include "function/render_vk/roc_single_vk.hh"
#include "generated/type_uuid.gen.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

namespace {

DebugDataArray parse_json_array(std::string_view p_json) {
    ParserJSON parser;
    parser.parse_from_buffer(p_json);
    return parser.get<DebugDataArray>();
}

DebugData make_descriptor_row(
    std::string_view p_resource_type, std::string_view p_descriptor_type, std::string_view p_heap, uint32_t p_heap_index, bool p_has_address) {
    DebugData row;
    row.set_value("resource_type", std::string(p_resource_type));
    row.set_value("descriptor_type", std::string(p_descriptor_type));
    row.set_value("heap", std::string(p_heap));
    row.set_value("heap_index", p_heap_index);
    if (p_has_address) {
        row.set_value("device_address", "0x1234");
    } else {
        row.set_value("device_address", nullptr);
    }
    return row;
}

} // namespace

class TestContextResourceVK final : public IRenderObjectVK {
public:
    TestContextResourceVK(const RenderContextVK* p_render_data, uint32_t p_context_id)
        : IRenderObjectVK(p_render_data), context_id(p_context_id) {
    }
    virtual ~TestContextResourceVK() override = default;
    TestContextResourceVK(const TestContextResourceVK&) = delete;
    TestContextResourceVK(TestContextResourceVK&&) noexcept = delete;
    TestContextResourceVK& operator=(const TestContextResourceVK&) = delete;
    TestContextResourceVK& operator=(TestContextResourceVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    uint32_t context_id;
};

class TestDescriptorResourceVK final : public IRenderObjectVK {
public:
    TestDescriptorResourceVK(const RenderContextVK* p_render_data, DebugDataArray p_rows)
        : IRenderObjectVK(p_render_data), rows(p_render_data->allocator) {
        rows.reserve(p_rows.size());
        for (DebugData& row : p_rows) {
            rows.emplace_back(std::move(row));
        }
    }
    virtual ~TestDescriptorResourceVK() override = default;
    TestDescriptorResourceVK(const TestDescriptorResourceVK&) = delete;
    TestDescriptorResourceVK(TestDescriptorResourceVK&&) noexcept = delete;
    TestDescriptorResourceVK& operator=(const TestDescriptorResourceVK&) = delete;
    TestDescriptorResourceVK& operator=(TestDescriptorResourceVK&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    virtual DebugDataRows descriptor_debug_data() const override {
        return rows;
    }

private:
    DebugDataRows rows;
};

template <>
struct TypeUUID<TestContextResourceVK> {
    using Type = TestContextResourceVK;
    static constexpr TypeID UUID = static_hash("TestContextResourceVK");
};

template <>
struct TypeUUID<TestDescriptorResourceVK> {
    using Type = TestDescriptorResourceVK;
    static constexpr TypeID UUID = static_hash("TestDescriptorResourceVK");
};

TypeID TestContextResourceVK::get_type_uuid() const {
    return TypeUUID<TestContextResourceVK>::UUID;
}

TypeID TestDescriptorResourceVK::get_type_uuid() const {
    return TypeUUID<TestDescriptorResourceVK>::UUID;
}

namespace {

// Factory of one TestContextResourceVK per slot, identified by p_first_id plus the slot index.
RenderObjectContainerVK::Factory make_context_factory(
    RenderAllocator* p_allocator, const RenderContextVK* p_render_data, uint32_t p_first_id = 0) {
    return [p_allocator, p_render_data, p_first_id](uint32_t p_slot) {
        return make_ref<TestContextResourceVK>(p_allocator, p_render_data, p_first_id + p_slot).template dynamic_cast_ref<IRenderObjectVK>();
    };
}

// Factory of TestDescriptorResourceVK objects without descriptor rows.
RenderObjectContainerVK::Factory make_empty_descriptor_factory(RenderAllocator* p_allocator, const RenderContextVK* p_render_data) {
    return [p_allocator, p_render_data](uint32_t /*p_slot*/) {
        return make_ref<TestDescriptorResourceVK>(p_allocator, p_render_data, DebugDataArray{}).template dynamic_cast_ref<IRenderObjectVK>();
    };
}

} // namespace

TEST(WBEROCMultislotsVKTest, CreatesOneResourcePerSlotAndDefaultsToFirstSlot) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    render_data.frames_in_flight = 3;

    ROCMultislotsVK context(&allocator, 3, ROCMultislotsVK::Factory{[&render_data, &allocator](uint32_t p_slot) {
        return make_ref<TestContextResourceVK>(&allocator, &render_data, p_slot).template dynamic_cast_ref<IRenderObjectVK>();
    }});

    EXPECT_EQ(context.get_resource_type_id(), TypeUUID<TestContextResourceVK>::UUID);
    EXPECT_EQ(context.get_slot_count(), 3U);

    auto default_resource = context.get().dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(default_resource, MEM_NULL);
    EXPECT_EQ(default_resource->get_type_uuid(), TypeUUID<TestContextResourceVK>::UUID);
    EXPECT_EQ(default_resource->context_id, 0U);

    auto explicit_resource = context.get(2).dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(explicit_resource, MEM_NULL);
    EXPECT_EQ(explicit_resource->context_id, 2U);
}

TEST(WBEROCMultislotsVKTest, ThrowsWhenFactoryIsMissingOrReturnsNull) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    EXPECT_THROW(ROCMultislotsVK(&allocator, 2, ROCMultislotsVK::Factory{}), std::runtime_error);
    EXPECT_THROW(ROCMultislotsVK(&allocator, 2, ROCMultislotsVK::Factory{[](uint32_t /*p_slot*/) {
        return Ref<IRenderObjectVK>(MEM_NULL);
    }}),
        std::runtime_error);
    EXPECT_THROW(ROCMultislotsVK(nullptr, 2, make_context_factory(&allocator, &render_data)), std::runtime_error);
    EXPECT_THROW(ROCMultislotsVK(&allocator, 0, make_context_factory(&allocator, &render_data)), std::runtime_error);
}

TEST(WBEROCMultislotsVKTest, UpdatesOneSlotIndependently) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    ROCMultislotsVK context(&allocator, 3, make_context_factory(&allocator, &render_data, 100));
    auto slot_zero_resource =
        make_ref<TestContextResourceVK>(&allocator, &render_data, 0).template dynamic_cast_ref<IRenderObjectVK>();
    auto slot_two_resource =
        make_ref<TestContextResourceVK>(&allocator, &render_data, 2).template dynamic_cast_ref<IRenderObjectVK>();

    context.set(0, slot_zero_resource);
    context.set(2, slot_two_resource);
    context.clear(1);

    auto default_resource = context.get().dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(default_resource, MEM_NULL);
    EXPECT_EQ(default_resource->context_id, 0U);

    auto explicit_resource = context.get(0).dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(explicit_resource, MEM_NULL);
    EXPECT_EQ(explicit_resource->context_id, 0U);

    EXPECT_EQ(context.get(1), MEM_NULL);
}

TEST(WBEROCMultislotsVKTest, NoFlushModeStillSupportsPerSlotReplacement) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    ROCMultislotsVK context(&allocator, 2, make_context_factory(&allocator, &render_data, 100), false);
    auto slot_zero_resource =
        make_ref<TestContextResourceVK>(&allocator, &render_data, 10).template dynamic_cast_ref<IRenderObjectVK>();
    auto slot_one_resource =
        make_ref<TestContextResourceVK>(&allocator, &render_data, 11).template dynamic_cast_ref<IRenderObjectVK>();
    auto replacement_resource =
        make_ref<TestContextResourceVK>(&allocator, &render_data, 42).template dynamic_cast_ref<IRenderObjectVK>();

    context.set(0, slot_zero_resource);
    context.set(1, slot_one_resource);
    context.set(1, replacement_resource);

    auto default_resource = context.get().dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(default_resource, MEM_NULL);
    EXPECT_EQ(default_resource->context_id, 10U);

    auto other_resource = context.get(0).dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(other_resource, MEM_NULL);
    EXPECT_EQ(other_resource->context_id, 10U);
}

TEST(WBEROCMultislotsVKTest, RejectsEmptyPublishedBufferUpdate) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    ROCMultislotsVK context(&allocator, 2, make_context_factory(&allocator, &render_data));

    EXPECT_THROW(context.publish_buffer_update(nullptr, 0, 1), std::runtime_error);
    uint8_t value = 0;
    EXPECT_THROW(context.publish_buffer_update(&value, 0, 0), std::runtime_error);
}

TEST(WBEROCSingleVKTest, ReturnsStoredResource) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    auto resource = make_ref<TestContextResourceVK>(&allocator, &render_data, 7).template dynamic_cast_ref<IRenderObjectVK>();
    ROCSingleVK container([resource](uint32_t /*p_slot*/) {
        return resource;
    });

    EXPECT_EQ(container.get_resource_type_id(), TypeUUID<TestContextResourceVK>::UUID);

    auto stored_resource = container.get().dynamic_cast_ref<TestContextResourceVK>();
    ASSERT_NE(stored_resource, MEM_NULL);
    EXPECT_EQ(stored_resource->context_id, 7U);
}

TEST(WBEROCSingleVKTest, ToStringOmitsSlotIndexAndPreservesMultipleDescriptors) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    auto resource = make_ref<TestDescriptorResourceVK>(&allocator,
        &render_data,
        DebugDataArray{make_descriptor_row("ROBufferVK", "uniform_buffer", "resource", 13, true),
            make_descriptor_row("ROImageViewVK", "sampled_image", "resource", 17, false)})
                        .template dynamic_cast_ref<IRenderObjectVK>();

    ROCSingleVK container([resource](uint32_t /*p_slot*/) {
        return resource;
    });
    DebugDataArray rows = parse_json_array(container.to_string());

    ASSERT_EQ(rows.size(), 2U);
    EXPECT_FALSE(rows[0].contains("slot_index"));
    EXPECT_EQ(rows[0].get_value<std::string>("resource_type"), "ROBufferVK");
    EXPECT_EQ(rows[0].get_value<std::string>("descriptor_type"), "uniform_buffer");
    EXPECT_EQ(rows[0].get_value<std::string>("device_address"), "0x1234");
    EXPECT_EQ(rows[1].get_value<std::string>("resource_type"), "ROImageViewVK");
    EXPECT_EQ(rows[1].get_value<std::string>("descriptor_type"), "sampled_image");
    EXPECT_FALSE(rows[1].contains("slot_index"));
    EXPECT_EQ(rows[1].get_value<std::nullptr_t>("device_address"), nullptr);
}

TEST(WBEROCMultislotsVKTest, ToStringAddsSlotIndexAndSkipsEmptyResources) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    ROCMultislotsVK container(&allocator, 3, make_empty_descriptor_factory(&allocator, &render_data));
    container.clear(1);
    container.set(0,
        make_ref<TestDescriptorResourceVK>(
            &allocator, &render_data, DebugDataArray{make_descriptor_row("ROBufferVK", "uniform_buffer", "resource", 4, true)})
            .template dynamic_cast_ref<IRenderObjectVK>());
    container.set(2,
        make_ref<TestDescriptorResourceVK>(
            &allocator, &render_data, DebugDataArray{make_descriptor_row("ROSamplerVK", "sampler", "sampler", 1, false)})
            .template dynamic_cast_ref<IRenderObjectVK>());

    DebugDataArray rows = parse_json_array(container.to_string());

    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].get_value<uint32_t>("slot_index"), 0U);
    EXPECT_EQ(rows[0].get_value<uint32_t>("heap_index"), 4U);
    EXPECT_EQ(rows[1].get_value<uint32_t>("slot_index"), 2U);
    EXPECT_EQ(rows[1].get_value<std::string>("heap"), "sampler");
    EXPECT_EQ(rows[1].get_value<std::nullptr_t>("device_address"), nullptr);
}

TEST(WBEROCMultislotsVKTest, ToStringDoesNotTriggerFlushSynchronization) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);

    ROCMultislotsVK container(&allocator, 1, make_empty_descriptor_factory(&allocator, &render_data));
    container.set(0,
        make_ref<TestDescriptorResourceVK>(
            &allocator, &render_data, DebugDataArray{make_descriptor_row("ROBufferVK", "uniform_buffer", "resource", 9, true)})
            .template dynamic_cast_ref<IRenderObjectVK>());

    uint8_t value = 7;
    container.publish_buffer_update(&value, 0, sizeof(value));

    EXPECT_NO_THROW({
        DebugDataArray rows = parse_json_array(container.to_string());
        ASSERT_EQ(rows.size(), 1U);
        EXPECT_EQ(rows[0].get_value<uint32_t>("slot_index"), 0U);
    });
}

TEST(WBEROCSingleVKTest, EmptyDescriptorResourcesProduceEmptyJsonArray) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    auto resource =
        make_ref<TestDescriptorResourceVK>(&allocator, &render_data, DebugDataArray{}).template dynamic_cast_ref<IRenderObjectVK>();

    ROCSingleVK container([resource](uint32_t /*p_slot*/) {
        return resource;
    });
    DebugDataArray rows = parse_json_array(container.to_string());

    EXPECT_TRUE(rows.empty());
}

TEST(WBEROCSingleVKTest, RecreateReplacesTheResourceWithANewOneFromTheFactory) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    uint32_t created_count = 0;
    ROCSingleVK container([&allocator, &render_data, &created_count](uint32_t p_slot) {
        EXPECT_EQ(p_slot, 0U);
        return make_ref<TestContextResourceVK>(&allocator, &render_data, created_count++).template dynamic_cast_ref<IRenderObjectVK>();
    });
    EXPECT_EQ(container.get().dynamic_cast_ref<TestContextResourceVK>()->context_id, 0U);

    container.recreate();

    EXPECT_EQ(created_count, 2U);
    EXPECT_EQ(container.get().dynamic_cast_ref<TestContextResourceVK>()->context_id, 1U);
}

TEST(WBEROCSingleVKTest, ThrowsWhenFactoryIsMissingOrReturnsNull) {
    EXPECT_THROW(ROCSingleVK(RenderObjectContainerVK::Factory{}), std::runtime_error);
    EXPECT_THROW(ROCSingleVK([](uint32_t /*p_slot*/) {
        return Ref<IRenderObjectVK>(MEM_NULL);
    }),
        std::runtime_error);
}

TEST(WBEROCMultislotsVKTest, RecreateReplacesEverySlotIncludingClearedAndReplacedOnes) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    uint32_t generation = 0;
    ROCMultislotsVK container(&allocator, 3, [&allocator, &render_data, &generation](uint32_t p_slot) {
        return make_ref<TestContextResourceVK>(&allocator, &render_data, generation * 10 + p_slot).template dynamic_cast_ref<IRenderObjectVK>();
    });
    container.clear(1);
    container.set(2, make_ref<TestContextResourceVK>(&allocator, &render_data, 99).template dynamic_cast_ref<IRenderObjectVK>());

    generation = 1;
    container.recreate();

    for (uint32_t slot = 0; slot < 3; ++slot) {
        auto resource = container.get(slot).dynamic_cast_ref<TestContextResourceVK>();
        ASSERT_NE(resource, MEM_NULL);
        EXPECT_EQ(resource->context_id, 10U + slot);
    }
}

} // namespace WhiteBirdEngine

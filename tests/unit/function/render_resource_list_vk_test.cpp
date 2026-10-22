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
#include "core/allocator/stl_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "function/render/i_render_object.hh"
#include "function/render/render_defs.hh"
#include "function/render_vk/render_resource_list_vk.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <gtest/gtest.h>
#include <stdexcept>

namespace WhiteBirdEngine {

namespace {

class RenderResourceListVKTestObject final : public IRenderObject {
public:
    RenderResourceListVKTestObject() = default;
    virtual ~RenderResourceListVKTestObject() override = default;
    RenderResourceListVKTestObject(const RenderResourceListVKTestObject&) = delete;
    RenderResourceListVKTestObject(RenderResourceListVKTestObject&&) noexcept = delete;
    RenderResourceListVKTestObject& operator=(const RenderResourceListVKTestObject&) = delete;
    RenderResourceListVKTestObject& operator=(RenderResourceListVKTestObject&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override {
        return static_hash("RenderResourceListVKTestObject");
    }
};

} // namespace

TEST(WBERenderResourceListVKTest, RetainsResourcesInInsertionOrder) {
    RenderAllocator allocator(WBE_KI_B(64));
    auto first = make_ref<RenderResourceListVKTestObject>(&allocator).dynamic_cast_ref<IRenderObject>();
    auto second = make_ref<RenderResourceListVKTestObject>(&allocator).dynamic_cast_ref<IRenderObject>();
    RenderResourceListVK resources(&allocator);

    resources.add_resource(first).add_resource(second);

    ASSERT_EQ(resources.get_resource_count(), 2U);
    EXPECT_EQ(resources.acquire_resource(0), first);
    EXPECT_EQ(resources.acquire_resource(1), second);
}

TEST(WBERenderResourceListVKTest, RejectsInvalidResourceAccess) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderResourceListVK resources(&allocator);

    EXPECT_THROW(resources.add_resource(MEM_NULL), std::runtime_error);
    EXPECT_THROW(resources.acquire_resource(0), std::runtime_error);
}

} // namespace WhiteBirdEngine

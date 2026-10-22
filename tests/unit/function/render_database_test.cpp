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
#include "function/render/i_render_object.hh"
#include "function/render/render_database.hh"
#include "function/render/render_defs.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <gtest/gtest.h>
#include <string_view>
#include <string>

namespace WhiteBirdEngine {

class RenderDatabaseTestRenderObject final : public IRenderObject {
public:
    RenderDatabaseTestRenderObject() = default;
    virtual ~RenderDatabaseTestRenderObject() override = default;
    RenderDatabaseTestRenderObject(const RenderDatabaseTestRenderObject&) = delete;
    RenderDatabaseTestRenderObject(RenderDatabaseTestRenderObject&&) noexcept = delete;
    RenderDatabaseTestRenderObject& operator=(const RenderDatabaseTestRenderObject&) = delete;
    RenderDatabaseTestRenderObject& operator=(RenderDatabaseTestRenderObject&&) noexcept = delete;

    virtual TypeID get_type_uuid() const override;

    std::string applied_debug_name;

protected:
    virtual void apply_debug_name(std::string_view p_name) override {
        applied_debug_name = p_name;
    }
};

TypeID RenderDatabaseTestRenderObject::get_type_uuid() const {
    return static_hash("RenderDatabaseTestRenderObject");
}

class WBERenderDatabaseTest : public ::testing::Test {
protected:
    RenderAllocator render_allocator{WBE_KI_B(128)};
    RenderDatabase render_database{&render_allocator};
};

TEST_F(WBERenderDatabaseTest, RegistersCompiledRenderResourceUsingConcreteObjectType) {
    Ref<RenderDatabaseTestRenderObject> render_object = make_ref<RenderDatabaseTestRenderObject>(&render_allocator);

    render_database.register_compiled_render_resource("test_resource", render_object);

    EXPECT_TRUE(render_database.contains_compiled_render_resource("test_resource", render_object->get_type_uuid()));
    EXPECT_EQ(render_database.get_compiled_render_resource("test_resource", render_object->get_type_uuid()), render_object);
}

TEST_F(WBERenderDatabaseTest, NamesRegisteredObjectsAfterTheirResourceKey) {
    Ref<RenderDatabaseTestRenderObject> object = make_ref<RenderDatabaseTestRenderObject>(&render_allocator);
    render_database.register_compiled_render_resource("sponza#vk_usage_130", object);
    EXPECT_EQ(object->applied_debug_name, "sponza#vk_usage_130");
    EXPECT_EQ(object->get_debug_name(), "sponza#vk_usage_130");
}

} // namespace WhiteBirdEngine

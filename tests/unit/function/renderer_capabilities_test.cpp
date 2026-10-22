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
#include "function/render/i_renderer.hh"
#include "function/render/render_defs.hh"
#include "function/render_dummy/renderer_dummy.hh"
#include "utils/defs.hh"
#include <future>
#include <gtest/gtest.h>
#include <optional>

namespace WhiteBirdEngine {

TEST(WBERendererCapabilitiesTest, DummyRendererDisablesUnsupportedCapabilities) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendererCapabilities capabilities{.conservative_rasterization = true, .ray_tracing = true, .ray_query = true, .timestamp_query = true, .descriptor_set = true};

    RendererDummy renderer(&allocator, capabilities);

    EXPECT_FALSE(capabilities.conservative_rasterization);
    EXPECT_FALSE(capabilities.ray_tracing);
    EXPECT_FALSE(capabilities.ray_query);
    EXPECT_FALSE(capabilities.timestamp_query);
    EXPECT_FALSE(capabilities.descriptor_set);
}

TEST(WBERendererCapabilitiesTest, DefaultTryAddJobAcceptsJobs) {
    RenderAllocator allocator(WBE_KI_B(64));
    RendererCapabilities capabilities{};
    RendererDummy renderer(&allocator, capabilities);

    std::optional<std::future<void>> result = renderer.try_add_job([]() {
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->valid());
}

} // namespace WhiteBirdEngine

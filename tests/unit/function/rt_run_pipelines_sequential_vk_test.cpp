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
#include "function/render/render_defs.hh"
#include "function/render_vk/ro_compute_pipeline_vk.hh"
#include "function/render_vk/rt_run_compute_pipelines_sequential_vk.hh"
#include "function/render_vk/rt_run_graphics_pipeline_indirect_vk.hh"
#include "function/render_vk/rt_run_graphics_pipelines_sequential_vk.hh"
#include "utils/defs.hh"
#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

TEST(WBERunPipelinesSequentialVKTest, ComputeDispatchAddsPipelineAndGroupCount) {
    RenderAllocator allocator(WBE_KI_B(64));
    RTRunComputePipelinesSequentialVK::Dispatch dispatch(&allocator);
    Ref<ROComputePipelineVK> pipeline = MEM_NULL;
    const VkExtent3D group_count{.width = 4, .height = 3, .depth = 2};

    RTRunComputePipelinesSequentialVK::Dispatch& result = dispatch.add_entry(pipeline, group_count);

    EXPECT_EQ(&result, &dispatch);
    ASSERT_EQ(dispatch.entries.size(), 1U);
    EXPECT_EQ(dispatch.entries[0].pipeline, pipeline);
    EXPECT_EQ(dispatch.entries[0].group_count.width, group_count.width);
    EXPECT_EQ(dispatch.entries[0].group_count.height, group_count.height);
    EXPECT_EQ(dispatch.entries[0].group_count.depth, group_count.depth);
}

TEST(WBERunPipelinesSequentialVKTest, GraphicsSequenceKeepsRegularAndIndirectDispatchTypes) {
    RenderAllocator allocator(WBE_KI_B(64));
    RTRunGraphicsPipelinesSequentialVK::Sequence sequence(&allocator);
    RTRunGraphicsPipelinesSequentialVK::RegularDispatch regular_dispatch(&allocator);
    regular_dispatch.render_extent = {.width = 1280, .height = 720};
    RTRunGraphicsPipelinesSequentialVK::IndirectDispatch indirect_dispatch(&allocator);
    indirect_dispatch.vertex_buffer_ids = {"vertices"};
    indirect_dispatch.vertex_buffer_offsets = {0};
    indirect_dispatch.index_buffer_id = "indices";
    indirect_dispatch.indirect_buffer_id = "draw_batches";
    indirect_dispatch.first_indirect_draw = 4;
    indirect_dispatch.indirect_draw_count = 12;

    RTRunGraphicsPipelinesSequentialVK::Sequence& result =
        sequence.add_dispatch(std::move(regular_dispatch)).add_dispatch(std::move(indirect_dispatch));

    EXPECT_EQ(&result, &sequence);
    ASSERT_EQ(sequence.get_dispatch_count(), 2U);
    EXPECT_FALSE(sequence.is_indirect(0));
    EXPECT_TRUE(sequence.is_indirect(1));
}

} // namespace WhiteBirdEngine

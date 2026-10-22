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
#include "function/render/render_defs.hh"
#include "function/render_vk/mesh_resources_vk.hh"
#include "utils/defs.hh"
#include <gtest/gtest.h>
#include <stdexcept>

namespace WhiteBirdEngine {

TEST(WBEMeshResourcesVKTest, StartsWithIndependentEmptyResources) {
    RenderAllocator allocator(WBE_KI_B(64));
    RenderContextVK render_data(&allocator);
    MeshResourcesVK resources(&render_data);

    EXPECT_EQ(resources.get_vertex_buffer_count(), 0U);
    EXPECT_THROW(resources.acquire_vertex_buffer(0), std::runtime_error);
    EXPECT_THROW(resources.get_vertex_buffer_index(MEM_NULL), std::runtime_error);
    EXPECT_EQ(MeshResourcesVK::get_vertex_buffer_member(1), "vertices.1");
    EXPECT_EQ(resources.acquire_indices(), MEM_NULL);
    EXPECT_EQ(resources.acquire_opaque_draw_batches(), MEM_NULL);
    EXPECT_EQ(resources.acquire_opaque_double_sided_draw_batches(), MEM_NULL);
    EXPECT_EQ(resources.acquire_masked_draw_batches(), MEM_NULL);
    EXPECT_NE(resources.acquire_textures(), MEM_NULL);
    EXPECT_EQ(resources.get_opaque_draw_batch_count(), 0U);
    EXPECT_EQ(resources.get_opaque_double_sided_draw_batch_count(), 0U);
    EXPECT_EQ(resources.get_masked_draw_batch_count(), 0U);
    EXPECT_THROW(resources.get_opaque_draw_batch(0), std::runtime_error);
    EXPECT_THROW(resources.get_opaque_double_sided_draw_batch(0), std::runtime_error);
    EXPECT_THROW(resources.get_masked_draw_batch(0), std::runtime_error);
}

} // namespace WhiteBirdEngine

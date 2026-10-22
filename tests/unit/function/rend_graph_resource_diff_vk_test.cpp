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
#include "function/render_vk/rend_graph_resource_diff_vk.hh"
#include "function/render_vk/render_graph_descriptor_vk.hh"
#include "function/render_vk/render_vk_defs.hh"
#include "utils/defs.hh"
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace WhiteBirdEngine {

namespace {

RendResSignatureVK buffer_signature(size_t p_size, bool p_per_frame = false) {
    return RendResSignatureVK{
        .kind = RendResKindVK::BUFFER,
        .desc = RendBufferDescVK{.size = p_size, .per_frame = p_per_frame},
        .per_frame = p_per_frame,
    };
}

RendResSignatureVK image_signature(uint32_t p_width, VkFormat p_format = VK_FORMAT_R8G8B8A8_UNORM) {
    return RendResSignatureVK{
        .kind = RendResKindVK::IMAGE,
        .desc = RendImageDescVK{.format = p_format, .extent = {.width = p_width, .height = 4, .depth = 1}},
    };
}

RendResSignatureVK view_signature(std::string_view p_image_id, uint32_t p_mip_level_count = 1) {
    return RendResSignatureVK{
        .kind = RendResKindVK::IMAGE_VIEW,
        .desc = RendImageViewDescVK{.mip_level_count = p_mip_level_count},
        .image_id = std::string(p_image_id),
    };
}

RendResSignatureVK import_signature(const void* p_identity) {
    return RendResSignatureVK{.kind = RendResKindVK::IMPORT, .import_identity = p_identity};
}

class WBERendGraphResourceDiffVKTest : public ::testing::Test {
protected:
    RendResRebuildActionVK action_of(std::string_view p_resource_id) const {
        const RendGraphResourceDiffVK::Actions actions = RendGraphResourceDiffVK::plan(previous, next, &allocator);
        const auto action = actions.find(p_resource_id);
        EXPECT_NE(action, actions.end()) << p_resource_id;
        return action->second;
    }

    mutable RenderAllocator allocator{WBE_KI_B(64)};
    RendGraphResourceDiffVK::Signatures previous{&allocator};
    RendGraphResourceDiffVK::Signatures next{&allocator};
};

} // namespace

TEST_F(WBERendGraphResourceDiffVKTest, NewIdsAreCreatedAndUnchangedOnesKept) {
    previous.emplace("kept", buffer_signature(64));
    next.emplace("kept", buffer_signature(64));
    next.emplace("added", buffer_signature(64));
    EXPECT_EQ(action_of("kept"), RendResRebuildActionVK::KEEP);
    EXPECT_EQ(action_of("added"), RendResRebuildActionVK::CREATE);
    // Ids missing from the next build are not planned; the graph releases them.
    previous.emplace("removed", buffer_signature(16));
    EXPECT_EQ(RendGraphResourceDiffVK::plan(previous, next, &allocator).count("removed"), 0U);
}

TEST_F(WBERendGraphResourceDiffVKTest, ConfigurationChangesRecreateInPlace) {
    previous.emplace("buffer", buffer_signature(64));
    next.emplace("buffer", buffer_signature(128));
    previous.emplace("image", image_signature(8));
    next.emplace("image", image_signature(8, VK_FORMAT_R16G16B16A16_SFLOAT));
    previous.emplace("sampler", RendResSignatureVK{.kind = RendResKindVK::SAMPLER, .desc = RendSamplerDescVK{}});
    next.emplace("sampler", RendResSignatureVK{.kind = RendResKindVK::SAMPLER, .desc = RendSamplerDescVK{.min_filter = VK_FILTER_NEAREST}});
    EXPECT_EQ(action_of("buffer"), RendResRebuildActionVK::RECREATE);
    EXPECT_EQ(action_of("image"), RendResRebuildActionVK::RECREATE);
    EXPECT_EQ(action_of("sampler"), RendResRebuildActionVK::RECREATE);
}

TEST_F(WBERendGraphResourceDiffVKTest, KindAndPerFrameChangesReplaceTheContainer) {
    // The same id becoming another kind of resource is never matched against the old one.
    previous.emplace("shared_id", buffer_signature(64));
    next.emplace("shared_id", image_signature(64));
    previous.emplace("frames", buffer_signature(64));
    next.emplace("frames", buffer_signature(64, true));
    EXPECT_EQ(action_of("shared_id"), RendResRebuildActionVK::REPLACE);
    EXPECT_EQ(action_of("frames"), RendResRebuildActionVK::REPLACE);
}

TEST_F(WBERendGraphResourceDiffVKTest, ImportsAreKeptOnlyForTheSameObject) {
    const int first_object = 0;
    const int second_object = 0;
    previous.emplace("same", import_signature(&first_object));
    next.emplace("same", import_signature(&first_object));
    previous.emplace("rebound", import_signature(&first_object));
    next.emplace("rebound", import_signature(&second_object));
    // An owned resource replaced by an import of the same name, and the reverse.
    previous.emplace("now_imported", buffer_signature(64));
    next.emplace("now_imported", import_signature(&first_object));
    previous.emplace("now_owned", import_signature(&first_object));
    next.emplace("now_owned", buffer_signature(64));
    EXPECT_EQ(action_of("same"), RendResRebuildActionVK::KEEP);
    EXPECT_EQ(action_of("rebound"), RendResRebuildActionVK::REPLACE);
    EXPECT_EQ(action_of("now_imported"), RendResRebuildActionVK::REPLACE);
    EXPECT_EQ(action_of("now_owned"), RendResRebuildActionVK::REPLACE);
}

TEST_F(WBERendGraphResourceDiffVKTest, ViewsFollowTheirImage) {
    previous.emplace("image", image_signature(8));
    previous.emplace("other_image", image_signature(8));
    previous.emplace("view", view_signature("image"));
    next.emplace("other_image", image_signature(8));
    next.emplace("view", view_signature("image"));

    // The image is recreated, so the unchanged view is recreated for the new image object.
    next.emplace("image", image_signature(16));
    EXPECT_EQ(action_of("view"), RendResRebuildActionVK::RECREATE);
    // Kept image, unchanged view: kept.
    next.insert_or_assign("image", image_signature(8));
    EXPECT_EQ(action_of("view"), RendResRebuildActionVK::KEEP);
    // The view's own configuration, or the image it views, changed.
    next.insert_or_assign("view", view_signature("image", 2));
    EXPECT_EQ(action_of("view"), RendResRebuildActionVK::RECREATE);
    next.insert_or_assign("view", view_signature("other_image"));
    EXPECT_EQ(action_of("view"), RendResRebuildActionVK::RECREATE);
}

TEST_F(WBERendGraphResourceDiffVKTest, ViewsOfReplacedImportsAreRecreated) {
    const int first_image = 0;
    const int second_image = 0;
    previous.emplace("imported_image", import_signature(&first_image));
    previous.emplace("view", view_signature("imported_image"));
    next.emplace("imported_image", import_signature(&second_image));
    next.emplace("view", view_signature("imported_image"));
    EXPECT_EQ(action_of("view"), RendResRebuildActionVK::RECREATE);
}

} // namespace WhiteBirdEngine

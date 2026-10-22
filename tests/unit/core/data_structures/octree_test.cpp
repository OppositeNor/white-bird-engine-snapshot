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
#include "core/allocator/heap_allocator_ram.hh"
#include "core/data_structures/octree.hh"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <utility>

using WhiteBirdEngine::HeapAllocatorRAM;
using WhiteBirdEngine::Octree;

TEST(WBEOctreeTest, EmptyTreeHasRootAndReleasesNodes) {
    HeapAllocatorRAM allocator;
    {
        Octree<int> tree(&allocator);
        auto root = tree.get_node({0.0F, 0.0F, 0.0F});
        ASSERT_NE(root, nullptr);
        EXPECT_FALSE(root->value.has_value());
        EXPECT_FALSE(root->parent.is_valid());
        root = nullptr;
    }
    EXPECT_TRUE(allocator.is_empty());
}

TEST(WBEOctreeTest, RootValueCanBeReplacedWithoutRemovingChildren) {
    HeapAllocatorRAM allocator;
    Octree<int> tree(&allocator);
    tree.insert(7, {0.0F, 0.0F, 0.0F}, 0);
    tree.insert(11, {0.75F, 0.75F, 0.75F}, 1);
    tree.insert(9, {0.0F, 0.0F, 0.0F}, 0);

    auto root = tree.get_node({0.75F, 0.75F, 0.75F}, 0);
    ASSERT_TRUE(root->value.has_value());
    EXPECT_EQ(*root->value, 9);
    EXPECT_EQ(*tree.get_node({0.75F, 0.75F, 0.75F})->value, 11);
}

TEST(WBEOctreeTest, DistinguishesAllEightOctantsAndLinksParents) {
    HeapAllocatorRAM allocator;
    Octree<int> tree(&allocator);
    const std::array<glm::vec3, 8> positions = {
        glm::vec3{0.25F, 0.25F, 0.25F},
        glm::vec3{0.25F, 0.25F, 0.75F},
        glm::vec3{0.25F, 0.75F, 0.25F},
        glm::vec3{0.25F, 0.75F, 0.75F},
        glm::vec3{0.75F, 0.25F, 0.25F},
        glm::vec3{0.75F, 0.25F, 0.75F},
        glm::vec3{0.75F, 0.75F, 0.25F},
        glm::vec3{0.75F, 0.75F, 0.75F},
    };
    for (size_t i = 0; i < positions.size(); ++i) {
        tree.insert(static_cast<int>(i), positions[i], 1);
    }

    auto root = tree.get_node(positions[0], 0);
    for (size_t i = 0; i < positions.size(); ++i) {
        auto node = tree.get_node(positions[i]);
        ASSERT_NE(node, nullptr);
        ASSERT_TRUE(node->value.has_value());
        EXPECT_EQ(*node->value, i);
        EXPECT_EQ(node.get(), root->children[i].get());
        EXPECT_EQ(node->parent.lock().get(), root.get());
    }
}

TEST(WBEOctreeTest, TraversesChildLocalCoordinatesAndReturnsExistingAncestor) {
    HeapAllocatorRAM allocator;
    Octree<int> tree(&allocator);
    tree.insert(42, {0.625F, 0.625F, 0.625F}, 2);

    auto root = tree.get_node({0.625F, 0.625F, 0.625F}, 0);
    auto first_level = tree.get_node({0.625F, 0.625F, 0.625F}, 1);
    auto second_level = tree.get_node({0.625F, 0.625F, 0.625F}, 2);
    EXPECT_FALSE(first_level->value.has_value());
    ASSERT_TRUE(second_level->value.has_value());
    EXPECT_EQ(*second_level->value, 42);
    EXPECT_EQ(first_level->parent.lock().get(), root.get());
    EXPECT_EQ(second_level->parent.lock().get(), first_level.get());
    EXPECT_EQ(tree.get_node({0.625F, 0.625F, 0.625F}, 9).get(), second_level.get());
    EXPECT_EQ(tree.get_node({0.25F, 0.625F, 0.625F}).get(), root.get());
}

TEST(WBEOctreeTest, RemapsEachAxisAfterDescending) {
    HeapAllocatorRAM allocator;
    Octree<int> tree(&allocator);
    const glm::vec3 position{0.375F, 0.625F, 0.875F};
    tree.insert(17, position, 2);

    auto root = tree.get_node(position, 0);
    auto first_level = root->children[Octree<int>::OctreeNode::NX_PY_PZ];
    ASSERT_NE(first_level, nullptr);
    auto second_level = first_level->children[Octree<int>::OctreeNode::PX_NY_PZ];
    ASSERT_NE(second_level, nullptr);
    EXPECT_EQ(tree.get_node(position).get(), second_level.get());
    EXPECT_EQ(*second_level->value, 17);
}

TEST(WBEOctreeTest, CenterPlanesBelongToUpperOctants) {
    HeapAllocatorRAM allocator;
    Octree<int> tree(&allocator);
    tree.insert(1, {0.0F, 0.0F, 0.0F}, 1);
    tree.insert(2, {0.5F, 0.5F, 0.5F}, 1);

    auto root = tree.get_node({0.0F, 0.0F, 0.0F}, 0);
    EXPECT_EQ(*root->children[Octree<int>::OctreeNode::NX_NY_NZ]->value, 1);
    EXPECT_EQ(*root->children[Octree<int>::OctreeNode::PX_PY_PZ]->value, 2);
}

TEST(WBEOctreeTest, MovingTreeTransfersNodeOwnership) {
    HeapAllocatorRAM allocator;
    {
        Octree<int> source(&allocator);
        source.insert(5, {0.75F, 0.75F, 0.75F}, 1);
        Octree<int> destination(std::move(source));
        EXPECT_EQ(*destination.get_node({0.75F, 0.75F, 0.75F})->value, 5);
    }
    EXPECT_TRUE(allocator.is_empty());
}

TEST(WBEOctreeTest, RejectsInvalidConstruction) {
    EXPECT_THROW((Octree<int>(nullptr)), std::runtime_error);
}

TEST(WBEOctreeTest, RejectsPositionsOutsideRootAndUnrepresentableDepth) {
    HeapAllocatorRAM allocator;
    Octree<int> tree(&allocator);
    EXPECT_THROW(tree.insert(1, {1.0F, 0.0F, 0.0F}, 1), std::runtime_error);
    EXPECT_THROW(tree.insert(1, {0.0F, -0.1F, 0.0F}, 1), std::runtime_error);
    EXPECT_THROW(tree.get_node({0.0F, 0.0F, 1.0F}), std::runtime_error);
    EXPECT_THROW(tree.get_node({std::numeric_limits<float>::infinity(), 0.0F, 0.0F}), std::runtime_error);
    EXPECT_THROW(tree.get_node({0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F}), std::runtime_error);
    const float upper_inside = std::nextafter(1.0F, 0.0F);
    tree.insert(2, {upper_inside, upper_inside, upper_inside}, 1);
    EXPECT_EQ(*tree.get_node({upper_inside, upper_inside, upper_inside})->value, 2);
    EXPECT_THROW(tree.insert(1, {0.0F, 0.0F, 0.0F}, UINT32_MAX), std::runtime_error);
    EXPECT_FALSE(tree.get_node({0.0F, 0.0F, 0.0F})->value.has_value());
}

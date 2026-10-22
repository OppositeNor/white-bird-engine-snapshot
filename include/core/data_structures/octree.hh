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
#ifndef WBE_FILE_OCTREE_HH
#define WBE_FILE_OCTREE_HH

#include "core/allocator/heap_allocator.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/reference_weak.hh"
#include "glm/common.hpp"
#include "glm/ext/vector_float3.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
namespace WhiteBirdEngine {

/**
 * @class Octree
 * @brief Stores values in nodes of a cubic octree.
 *
 * The root covers SVO coordinates [0, 1) on each axis. The tree owns its nodes, while the allocator must outlive the tree
 * and all node references returned by get_node(). Inserting at an occupied node replaces its value. A node may have both
 * a value and children. Trees can be moved but not copied.
 *
 * @tparam T The type of the values in the nodes.
 */
template <typename T>
class Octree {
public:
    /**
     * @struct OctreeNode
     * @brief A node with an optional value and up to eight children.
     * Nodes cannot be copied or moved because their child ownership and parent links must remain connected.
     */
    struct OctreeNode {
        /**
         * @brief Create an empty node.
         */
        OctreeNode() = default;
        /**
         * @brief Release the owned child nodes.
         */
        ~OctreeNode() = default;
        /**
         * @brief Node ownership cannot be copied.
         */
        OctreeNode(const OctreeNode&) = delete;
        /**
         * @brief Node ownership cannot be copied.
         */
        OctreeNode& operator=(const OctreeNode&) = delete;
        /**
         * @brief Node identity and parent links cannot be moved.
         */
        OctreeNode(OctreeNode&&) = delete;
        /**
         * @brief Node identity and parent links cannot be moved.
         */
        OctreeNode& operator=(OctreeNode&&) = delete;

        /**
         * @enum Subregion
         * @brief Child octants in x, y, z bit order; P denotes the upper half and N the lower half.
         */
        enum Subregion : uint8_t {
            NX_NY_NZ,
            NX_NY_PZ,
            NX_PY_NZ,
            NX_PY_PZ,
            PX_NY_NZ,
            PX_NY_PZ,
            PX_PY_NZ,
            PX_PY_PZ,
            MAX_SUBREGION
        };
        /** The value stored directly in this node, if any. */
        std::optional<T> value = std::nullopt;
        /** The non-owning link to the parent; empty for the root. */
        RefWeak<const OctreeNode> parent;
        /** The owning links to child nodes, indexed by Subregion. */
        std::array<Ref<OctreeNode>, MAX_SUBREGION> children;
    };

    /**
     * @brief Create an empty tree with an allocated root node.
     * @param p_allocator Allocator that must outlive the tree and returned node references.
     * @throws std::runtime_error If the allocator is null.
     */
    explicit Octree(HeapAllocator* p_allocator) : allocator(p_allocator) {
        if (allocator == nullptr) {
            throw std::runtime_error("Octree requires a non-null allocator.");
        }
        root = make_ref<OctreeNode>(allocator);
    }

    /**
     * @brief Release the tree's nodes.
     */
    ~Octree() = default;
    /**
     * @brief Trees cannot be copied because they own their node hierarchy.
     */
    Octree(const Octree&) = delete;
    /**
     * @brief Trees cannot be copied because they own their node hierarchy.
     */
    Octree& operator=(const Octree&) = delete;
    /**
     * @brief Transfer ownership of the node hierarchy.
     */
    Octree(Octree&&) noexcept = default;
    /**
     * @brief Replace this tree's hierarchy with another tree's hierarchy.
     */
    Octree& operator=(Octree&&) noexcept = default;

    /**
     * @brief Store a value at the node selected by an SVO position and depth.
     * @tparam T1 Type of the value being forwarded.
     * @param p_val Value to store; replaces any value already at that node.
     * @param p_position SVO position in [0, 1) on each axis.
     * @param p_depth Number of child levels below the root; zero selects the root.
     * @throws std::runtime_error If the position lies outside [0, 1)^3 or the requested cubes become too small to
     * represent with float.
     */
    template <typename T1>
    void insert(T1&& p_val, glm::vec3 p_position, uint32_t p_depth) {
        check_position(p_position);
        float child_size = 1.0F;
        for (uint32_t i = 0; i < p_depth; ++i) {
            child_size /= 2.0F;
            if (child_size == 0.0F) {
                throw std::runtime_error("Octree insertion depth exceeds float range.");
            }
        }
        auto current = root;
        glm::vec3 position = p_position;
        for (uint32_t depth = 0; depth < p_depth; ++depth) {
            auto subregion = get_subregion(position);
            auto child = current->children[subregion];
            if (child == MEM_NULL) {
                child = make_ref<OctreeNode>(allocator);
                child->parent = current;
                current->children[subregion] = child;
            }
            current = child;
            position = glm::fract(position * 2.0F);
        }
        current->value = std::forward<T1>(p_val);
    }

    /**
     * @brief Get the node on a position's path at the requested depth, or the deepest existing ancestor.
     * @param p_position SVO position in [0, 1) on each axis.
     * @param p_depth Number of child levels to traverse; a negative value searches until a child is absent.
     * @return A read-only reference to the selected node, including an empty root for an empty tree.
     * @throws std::runtime_error If the position lies outside [0, 1)^3.
     */
    Ref<const OctreeNode> get_node(glm::vec3 p_position, int32_t p_depth = -1) const {
        check_position(p_position);
        Ref<const OctreeNode> current = root;
        glm::vec3 position = p_position;
        for (int32_t depth = 0; p_depth < 0 || depth < p_depth; ++depth) {
            auto child = current->children[get_subregion(position)];
            if (child == MEM_NULL) {
                break;
            }
            current = child;
            position = glm::fract(position * 2.0F);
        }
        return current;
    }

private:
    HeapAllocator* allocator = nullptr;
    Ref<OctreeNode> root;
    void check_position(glm::vec3 p_position) const {
        if (!std::isfinite(p_position.x) || !std::isfinite(p_position.y) || !std::isfinite(p_position.z) || p_position.x < 0.0F ||
            p_position.y < 0.0F || p_position.z < 0.0F || p_position.x >= 1.0F || p_position.y >= 1.0F || p_position.z >= 1.0F) {
            throw std::runtime_error("Octree position lies outside [0, 1)^3.");
        }
    }

    static OctreeNode::Subregion get_subregion(glm::vec3 p_position) {
        uint8_t subregion =
            (p_position.x >= 0.5F ? 0b100 : 0) | (p_position.y >= 0.5F ? 0b010 : 0) | (p_position.z >= 0.5F ? 0b001 : 0);
        return static_cast<OctreeNode::Subregion>(subregion);
    }
};

} // namespace WhiteBirdEngine

#endif

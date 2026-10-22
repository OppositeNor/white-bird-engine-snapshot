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
#ifndef WBE_FILE_RESOURCE_MANAGER_HH
#define WBE_FILE_RESOURCE_MANAGER_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stack_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_unit_loader/resource_unit_loader.hh"
#include "utils/interface/i_singleton.hh"
#include <cstddef>
#include <flat_map>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace WhiteBirdEngine {

/**
 * @class ResourceManager
 * @brief Manages the resources loaded and used by the engine.
 *
 * @todo Doc, Test
 */
class ResourceManager : public ISingleton<ResourceManager> {
public:
    ResourceManager() = delete;
    virtual ~ResourceManager() override;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) noexcept = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager& operator=(ResourceManager&&) noexcept = delete;

    /**
     * @brief Constructor.
     *
     * @param p_alloc_size The total capacity of the resource manager.
     * @param p_global_res_path The path to the global resources.
     */
    ResourceManager(size_t p_alloc_size);

    /**
     * @brief Register a resource chunk.
     *
     * @param p_chunk_id The id of the chunk, used to locate the chunk when
     * getting resources.
     * @param p_resource_chunk The resource chunk to register.
     */
    ResourceChunkID register_chunk(Ref<IResourceChunk> p_resource_chunk) {
        ResourceChunkID chunk_id = resource_chunks.size();
        resource_chunks.push_back(p_resource_chunk);
        return chunk_id;
    }

    /**
     * @brief Pop a resource chunk from the end of the chunk stack.
     */
    void pop_chunk() {
        resource_chunks.pop_back();
    }

    /**
     * @brief Get a resource from a chunk.
     *
     * @param p_chunk The resource chunk to get resource from.
     * @param p_key The key of the resource.
     */
    template <typename T>
    T* get_resource(ResourceChunkID p_chunk_id, std::string_view p_id) {
        return reinterpret_cast<T*>(get_resource_chunk(p_chunk_id)->get_resource(p_id).get());
    }

    /**
     * @brief Get the resource chunk according to the chunk's ID.
     *
     * @param p_chunk_id The ID of the chunk/
     * @return The resource chunk.
     */
    Ref<IResourceChunk> get_resource_chunk(ResourceChunkID p_chunk_id) {
        if (p_chunk_id >= resource_chunks.size()) {
            throw std::runtime_error("Failed to get resource chunk: chunk with ID " + std::to_string(p_chunk_id) + " not registered.");
        }
        return resource_chunks[p_chunk_id];
    }

    /**
     * @brief Get the resource chunk according to the chunk's ID.
     *
     * @param p_chunk_id The ID of the chunk/
     * @return The resource chunk.
     */
    Ref<const IResourceChunk> get_resource_chunk(ResourceChunkID p_chunk_id) const {
        if (p_chunk_id >= resource_chunks.size()) {
            throw std::runtime_error("Failed to get resource chunk: chunk with ID " + std::to_string(p_chunk_id) + " not registered.");
        }
        return resource_chunks[p_chunk_id];
    }

    /**
     * @brief Get the resource chunk according to the chunk's ID.
     *
     * @param p_id The ID of the chunk.
     * @return The resource chunk.
     */
    template <typename T>
    RefRes<T> get_resource(std::string_view p_id) const {
        return find_resource_chunk_containing_resource(p_id)->get_resource(p_id).force_cast<T>();
    }

    /**
     * @brief Look up a resource across every registered chunk without throwing.
     *
     * Searches all chunks currently registered with the manager and returns
     * the resource owned by the first chunk that contains @p p_id. This enables
     * global deduplication: a chunk being loaded can reuse a resource that an
     * already-registered chunk loaded instead of allocating a second copy.
     *
     * @param p_id The identifier of the resource to look up.
     * @return The resource reference, or a null reference when no registered
     * chunk contains the resource.
     */
    RefRes<char> try_get_global_resource(std::string_view p_id) const;

    /**
     * @brief Get a resource by its memory ID.
     *
     * @param p_memid The memory ID of the resource.
     */
    template <typename T>
    T* get_res_by_memid(MemID p_memid) {
        return stack_allocator.get_obj<T>(p_memid);
    }

    /**
     * @brief Get the singleton instance.
     *
     * @return The singleton instance. Null if not initialized.
     */
    static ResourceManager* get_singleton() {
        return singleton;
    }

    const StackAllocator& get_allocator() const {
        return stack_allocator;
    }

    StackAllocator& get_allocator() {
        return stack_allocator;
    }

    ResourceChunkID global_res_chunk() const {
        return global_res_chunk_id;
    }

    void add_resource_unit_loader(Ref<IResourceUnitLoader> p_resource_unit_loader) {
        for (const auto& resource_type : p_resource_unit_loader->get_supported_resource_types()) {
            if (!resource_unit_loaders.contains(resource_type)) {
                resource_unit_loaders.emplace(resource_type, p_resource_unit_loader);
                continue;
            }
            throw std::runtime_error(std::format("Failed to add resource loader: resource type '{}' "
                                                 "already has a loader.",
                resource_type));
        }
    }

    Ref<IResourceUnitLoader> get_resource_unit_loader(const std::string& p_resource_type) const {
        auto it = resource_unit_loaders.find(p_resource_type);
        if (it == resource_unit_loaders.end()) {
            throw std::runtime_error(std::format("No resource loader registered for resource type: {}.", p_resource_type));
        }
        return it->second;
    }

    void load_global_resource(std::string_view p_global_res_path);

private:
    ResourceChunkID global_res_chunk_id = RES_CHUNK_NULL;
    std::vector<Ref<IResourceChunk>> resource_chunks;
    std::flat_map<std::string, Ref<IResourceUnitLoader>> resource_unit_loaders;
    StackAllocator stack_allocator;

    Ref<IResourceChunk> find_resource_chunk_containing_resource(std::string_view p_id) const;

    inline static ResourceManager* singleton = nullptr;
};

} // namespace WhiteBirdEngine

#endif

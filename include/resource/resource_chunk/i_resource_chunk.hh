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
#ifndef WBE_FILE_I_RESOURCE_CHUNK_HH
#define WBE_FILE_I_RESOURCE_CHUNK_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/unique.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "utils/utils.hh"
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

class ResourceManager;

using ResourceChunkID = HashCode;
constexpr ResourceChunkID RES_CHUNK_NULL = 0x0;

/**
 * @class IResourceChunk
 * @brief Interface for chunk of resources. Used for containing multiple
 * resources to be loaded and deloaded at once.
 *
 * Resource units stored in a chunk are not required to be trivially
 * destructible. Chunks may reserve uninitialized storage, construct units
 * in place, and register destructors to be run before the backing allocator
 * memory is released.
 * @todo Test
 */
class IResourceChunk {
public:
    struct ResUnitStorage {
        RefRes<char> resource;
        const char* resource_key = nullptr;
    };

    IResourceChunk() = default;
    virtual ~IResourceChunk() = default;
    IResourceChunk(const IResourceChunk&) = default;
    IResourceChunk(IResourceChunk&&) noexcept = default;
    IResourceChunk& operator=(const IResourceChunk&) = default;
    IResourceChunk& operator=(IResourceChunk&&) noexcept = default;

    /**
     * @brief Get the resource from a key.
     *
     * @param p_id The identifier of the resource unit.
     * @return The resource reference of the resource.
     */
    virtual RefRes<char> get_resource(std::string_view p_id) const = 0;

    /**
     * @brief Look up a resource by key without throwing.
     *
     * Performs a single lookup and returns the resource if present, avoiding the
     * need to call contains() followed by get_resource().
     *
     * @param p_id The identifier of the resource unit.
     * @return The resource reference, or a null reference when the chunk does
     * not contain the resource.
     */
    virtual RefRes<char> try_get_resource(std::string_view p_id) const = 0;

    /**
     * @brief Get the total size of the chunk.
     *
     * @return The total size of the chunk.
     */
    virtual size_t get_size() const = 0;

    /**
     * @brief Push a chunk of data into the chunk. The data will be copied to
     * the allocator.
     *
     * @param p_res_size The size of the resource to push to.
     * @param p_res The pointer to the resource to be pushed into the chunk.
     * @param p_allocator The allocator to allocate memory.
     * @return The result memory id.
     */
    virtual RefRes<char> push_data(size_t p_res_size, const void* p_res) = 0;

    /**
     * @brief Get the data at ID.
     *
     * @param p_mem_id The ID to get the data from.
     * @return The pointer to the data to get.
     */
    virtual void* get_data(MemID p_mem_id) = 0;

    /**
     * @brief Get the data at ID.
     *
     * @param p_mem_id The ID to get the data from.
     * @return The pointer to the data to get.
     */
    virtual const void* get_data(MemID p_mem_id) const = 0;

    /**
     * @brief Get all resource keys in the chunk.
     */
    virtual std::vector<std::string> get_all_keys() const = 0;

    /**
     * @brief Check if the chunk contains a resource with the given key.
     *
     * @param p_res_key The key to check.
     * @return True if the chunk contains the resource, false otherwise.
     */
    virtual bool contains(std::string_view p_res_key) const = 0;

    /**
     * @brief Create a reference to a resource in the chunk.
     * This reference will be resolved after the resource chunk is loaded.
     *
     * @param p_ref The reference to store the resolved resource.
     * @param p_key The key of the resource to reference.
     */
    template <typename T>
    void create_ref_to(RefRes<T>& p_ref, std::string_view p_key) {
        create_ref_to_resolver(
            [&p_ref](RefRes<char> p_resource) {
                p_ref = p_resource.template force_cast<T>();
            },
            p_key);
    }

    /**
     * @brief Push a resource unit to the chunk.
     *
     * Stores a constructed copy of @p p_res inside chunk-owned storage. When
     * the resource unit type has a non-trivial destructor, the chunk will
     * register teardown so destruction happens before the chunk releases its
     * allocator memory.
     *
     * @tparam T The type of the resource unit.
     * @param p_id The identifier of the resource unit.
     * @param p_res The resource unit instance.
     * @return The resource reference to the stored resource unit.
     */
    template <ResourceUnitConcept T>
    RefRes<char> push_res_unit(std::string_view p_id, const T& p_res) {
        RefRes<T> stored_resource = emplace_res_unit<T>(p_id, p_res);
        return stored_resource.template force_cast<char>();
    }

    /**
     * @brief Construct a resource unit directly inside chunk-owned storage.
     *
     * This is the preferred path for resource units that need real
     * constructors or destructors. The chunk reserves storage, constructs the
     * unit in place, assigns the stable resource key, and registers a
     * destructor callback when @p T is not trivially destructible.
     *
     * @tparam T The type of the resource unit.
     * @param p_id The identifier of the resource unit.
     * @param p_args The arguments forwarded to the resource unit constructor.
     * @return The typed reference to the constructed resource unit.
     */
    template <ResourceUnitConcept T, typename... Args>
    RefRes<T> emplace_res_unit(std::string_view p_id, Args&&... p_args) {
        ResUnitStorage storage = reserve_res_unit_storage(p_id, sizeof(T));
        T* stored_resource = new (storage.resource.get()) T(std::forward<Args>(p_args)...);
        stored_resource->resource_key = storage.resource_key;
        if constexpr (!std::is_trivially_destructible_v<T>) {
            register_res_unit_destructor(storage.resource.get(), [](void* p_resource) {
                static_cast<T*>(p_resource)->~T();
            });
        }
        return storage.resource.template force_cast<T>();
    }

    /**
     * @brief Get the data at ID.
     *
     * @tparam T the type of the data to get.
     * @param p_mem_id The ID to get the data from.
     * @return The pointer to the data to get.
     */
    template <typename T>
    T* get_data(MemID p_mem_id) {
        return static_cast<T*>(get_data(p_mem_id));
    }

    /**
     * @brief Get the data at ID.
     *
     * @tparam T the type of the data to get.
     * @param p_mem_id The ID to get the data from.
     * @return The pointer to the data to get.
     */
    template <typename T>
    const T* get_data(MemID p_mem_id) const {
        return static_cast<const T*>(get_data(p_mem_id));
    }

protected:
    /**
     * @brief Create a type-erased reference resolver.
     *
     * @param p_assign_ref The callback that writes the resolved resource.
     * @param p_key The key of the resource to reference.
     */
    virtual void create_ref_to_resolver(std::function<void(RefRes<char>)> p_assign_ref, std::string_view p_key) = 0;

    /**
     * @brief Reserve uninitialized storage for a resource unit.
     *
     * @param p_res_key The key of the resource unit.
     * @param p_size The size of the resource unit.
     * @return The storage handle containing the reserved bytes and the
     * chunk-owned stable resource key.
     */
    virtual ResUnitStorage reserve_res_unit_storage(std::string_view p_res_key, size_t p_size) = 0;

    /**
     * @brief Register a destructor callback for a non-trivial resource unit.
     *
     * The callback will be invoked before the chunk releases the allocator
     * memory backing the resource unit.
     *
     * @param p_resource The resource instance to destroy.
     * @param p_destructor The type-erased destructor callback.
     */
    virtual void register_res_unit_destructor(void* p_resource, void (*p_destructor)(void*)) = 0;
};

} // namespace WhiteBirdEngine

#endif

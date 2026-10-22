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
#ifndef WBE_FILE_RESOURCE_CHUNK_LOADER_HH
#define WBE_FILE_RESOURCE_CHUNK_LOADER_HH
#include "core/allocator/i_allocator.hh"
#include "core/allocator/stack_allocator.hh"
#include "core/data/data_value.hh"
#include "core/memory/unique.hh"
#include "core/parser/parser_json.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_manager/resource_manager.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_utils.hh"
#include "utils/utils.hh"
#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

using ParserResource = ParserJSON;

/**
 * @class ResourceChunk
 * @brief Chunk of resources. Used for containing multiple resources to be
 * loaded and deloaded at once.
 *
 * Resource payload bytes are still stored in the chunk allocator, but resource
 * units themselves may now be constructed in place and can have non-trivial
 * destructors. Registered resource-unit destructors run in reverse
 * construction order before the allocator stack memory is popped.
 * @todo Test
 */
class ResourceChunkLoadFile final : public IResourceChunk {
public:
    ~ResourceChunkLoadFile() {
        for (const auto& resource_destructor : std::views::reverse(resource_destructors)) {
            resource_destructor.destroy(resource_destructor.resource);
        }
        allocator->pop_stack(total_size);
    }
    ResourceChunkLoadFile(const ResourceChunkLoadFile&) = delete;
    ResourceChunkLoadFile(ResourceChunkLoadFile&&) = delete;
    ResourceChunkLoadFile& operator=(const ResourceChunkLoadFile&) = delete;
    ResourceChunkLoadFile& operator=(ResourceChunkLoadFile&&) = delete;

    /**
     * @brief Constructor
     *
     * @param p_path The resource path to the resource file.
     * @param p_parser The parser to get resource from.
     */
    ResourceChunkLoadFile(std::string_view p_path, StackAllocator& p_allocator);

    virtual RefRes<char> get_resource(std::string_view p_id) const override {
        return resource_map.at(std::string(p_id));
    }

    virtual RefRes<char> try_get_resource(std::string_view p_id) const override {
        auto entry = resource_map.find(std::string(p_id));
        if (entry == resource_map.end()) {
            return MEM_NULL;
        }
        return entry->second;
    }

    virtual RefRes<char> push_data(size_t p_res_size, const void* p_res) override {
        total_size += p_res_size;
        MemID result_mem_id = allocator->allocate(p_res_size);
        auto* copy_to = allocator->get(result_mem_id);
        memcpy(copy_to, p_res, p_res_size);
        RefRes<char> result(result_mem_id, allocator);
        return result;
    }

    virtual size_t get_size() const override {
        return total_size;
    }

    virtual void* get_data(MemID p_mem_id) override {
        return allocator->get(p_mem_id);
    }

    virtual const void* get_data(MemID p_mem_id) const override {
        return allocator->get(p_mem_id);
    }

    virtual bool contains(std::string_view p_res_key) const override {
        return resource_map.contains(p_res_key);
    }

    virtual std::vector<std::string> get_all_keys() const override {
        std::vector<std::string> result;
        result.reserve(resource_map.size());
        for (const auto& [key, _] : resource_map) {
            result.push_back(key);
        }
        return result;
    }

protected:
    virtual void create_ref_to_resolver(std::function<void(RefRes<char>)> p_assign_ref, std::string_view p_id) override {
        auto entry = resource_map.find(p_id);
        if (entry != resource_map.end()) {
            p_assign_ref(entry->second);
            return;
        }
        ref_map.push_back(ResourceReference{.assign_ref = std::move(p_assign_ref), .id = std::string(p_id)});
    }

    virtual ResUnitStorage reserve_res_unit_storage(std::string_view p_res_key, size_t p_size) override {
        if (!p_res_key.empty() && resource_map.contains(p_res_key)) {
            throw std::runtime_error(std::format("Redefinition of key: {}.", p_res_key));
        }
        // Store the resource key inside the same chunk so ResourceUnit can
        // expose a stable, chunk-owned key without referencing manifest memory.
        char* resource_key = nullptr;
        if (!p_res_key.empty()) {
            total_size += p_res_key.size() + 1;
            MemID resource_key_mem_id = allocator->allocate(p_res_key.size() + 1);
            resource_key = allocator->get_obj<char>(resource_key_mem_id);
            memcpy(resource_key, p_res_key.data(), p_res_key.size() + 1);
            resource_key[p_res_key.size()] = '\0';
        }
        total_size += p_size;
        MemID result_mem_id = allocator->allocate(p_size);
        RefRes<char> result(result_mem_id, allocator);
        if (!p_res_key.empty()) {
            resource_map[std::string(p_res_key)] = result;
        }
        return ResUnitStorage{.resource = result, .resource_key = resource_key};
    }

    virtual void register_res_unit_destructor(void* p_resource, void (*p_destructor)(void*)) override {
        resource_destructors.push_back(ResourceDestructor{.resource = p_resource, .destroy = p_destructor});
    }

private:
    struct ResourceDestructor {
        void* resource;
        void (*destroy)(void*);
    };

    struct ResourceReference {
        std::function<void(RefRes<char>)> assign_ref;
        std::string id;
    };

    size_t total_size = 0;
    std::unordered_map<std::string, RefRes<char>, TransparentStrHash, TransparentEqual> resource_map;
    std::vector<ResourceReference> ref_map;
    std::vector<ResourceDestructor> resource_destructors;
    StackAllocator* allocator;

    void resolve_load_resource(std::vector<std::string>& p_included_pathes, std::string_view p_res_chunk_path);

    void resolve_refs() {
        auto* resource_manager = ResourceManager::get_singleton();
        for (auto& resource_reference : ref_map) {
            auto entry = resource_map.find(resource_reference.id);
            if (entry != resource_map.end()) {
                resource_reference.assign_ref(entry->second);
                continue;
            }
            // Not found in the current chunk, search every other registered
            // chunk so references can target resources owned globally.
            RefRes<char> resolved = resource_manager->try_get_global_resource(resource_reference.id);
            if (resolved.is_null()) {
                throw std::runtime_error(
                    std::format("Failed to resolve resource reference: no chunk contains key: {}.", resource_reference.id));
            }
            resource_reference.assign_ref(resolved);
        }
    }
};

inline void ResourceChunkLoadFile::resolve_load_resource(std::vector<std::string>& p_included_pathes, std::string_view p_res_chunk_path) {
    if (std::any_of(p_included_pathes.begin(), p_included_pathes.end(), [&](const auto& p_str) {
            return p_str == p_res_chunk_path;
        })) {
        throw std::runtime_error(std::format("Include recursion found: {}", p_res_chunk_path));
    }
    p_included_pathes.emplace_back(p_res_chunk_path);

    auto rc_file_path = get_resource_path(p_res_chunk_path);
    ParserResource parser;
    parser.parse(rc_file_path);

    for (const DataValue& res : parser.get_data().get_entry("resources").as_list_or_empty()) {
        // Use generic scheme only for quick type/id extraction during dispatch.
        RUSchema resource_scheme;
        resource_scheme.deserialize(res);
        if (!resource_scheme.id.empty()) {
            if (resource_map.contains(resource_scheme.id)) {
                // Already resolved before in this chunk, ignore.
                continue; // This might be a bit unsafe, but considered that C++
                          // also kind of handles its inline function like this,
                          // so I think this is fine. -- OppositeNor
            }
            // Deduplicate globally: if a chunk that is already registered has
            // loaded this resource, alias it into this chunk instead of loading
            // a second copy. Anonymous resources (empty id) cannot be shared, so
            // they always load locally.
            RefRes<char> existing = ResourceManager::get_singleton()->try_get_global_resource(resource_scheme.id);
            if (!existing.is_null()) {
                resource_map[resource_scheme.id] = existing;
                continue;
            }
        }
        ResourceManager::get_singleton()->get_resource_unit_loader(resource_scheme.type)->populate_scheme_from_data(*this, res);
    }

    if (parser.contains("include")) {
        // Resolve includes
        auto included_resource_chunks = parser.template get_value<std::vector<std::string>>("include");
        for (auto& include_path : included_resource_chunks) {
            resolve_load_resource(p_included_pathes, include_path);
        }
    }
    p_included_pathes.pop_back();
}

inline ResourceChunkLoadFile::ResourceChunkLoadFile(std::string_view p_path, StackAllocator& p_allocator) {
    allocator = &p_allocator;
    std::vector<std::string> included_pathes;
    resolve_load_resource(included_pathes, p_path);
    resolve_refs();
}

} // namespace WhiteBirdEngine

#endif

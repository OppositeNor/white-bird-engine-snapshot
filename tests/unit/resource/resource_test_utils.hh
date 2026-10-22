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
#include "core/memory/unique.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

class MockResourceChunk final : public WBE::IResourceChunk {
public:
    struct Builder {
        std::function<WBE::RefRes<char>(std::string_view)> get_resource_callback = nullptr;
        std::function<WBE::RefRes<char>(std::string_view)> try_get_resource_callback = nullptr;
        std::function<size_t()> get_size_callback = nullptr;
        std::function<WBE::RefRes<char>(size_t, const void*)> push_data_callback = nullptr;
        std::function<void*(WBE::MemID)> get_data_callback = nullptr;
        std::function<const void*(WBE::MemID)> get_data_const_callback = nullptr;
        std::function<std::vector<std::string>()> get_all_keys_callback = nullptr;
        std::function<bool(std::string_view)> contains_callback = nullptr;
        std::function<WBE::RefRes<char>(std::string_view, size_t)> reserve_res_unit_storage_callback = nullptr;
        std::function<void(void*, void (*)(void*))> register_res_unit_destructor_callback = nullptr;
        std::function<void(std::function<void(WBE::RefRes<char>)>, std::string_view)> create_ref_to_callback = nullptr;

        Builder& set_get_resource_callback(std::function<WBE::RefRes<char>(std::string_view)> p_callback) {
            get_resource_callback = p_callback;
            return *this;
        }

        Builder& set_try_get_resource_callback(std::function<WBE::RefRes<char>(std::string_view)> p_callback) {
            try_get_resource_callback = p_callback;
            return *this;
        }

        Builder& set_get_size_callback(std::function<size_t()> p_callback) {
            get_size_callback = p_callback;
            return *this;
        }

        Builder& set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)> p_callback) {
            push_data_callback = p_callback;
            return *this;
        }

        Builder& set_get_data_callback(std::function<void*(WBE::MemID)> p_callback) {
            get_data_callback = p_callback;
            return *this;
        }

        Builder& set_get_data_const_callback(std::function<const void*(WBE::MemID)> p_callback) {
            get_data_const_callback = p_callback;
            return *this;
        }

        Builder& set_get_all_keys_callback(std::function<std::vector<std::string>()> p_callback) {
            get_all_keys_callback = p_callback;
            return *this;
        }

        Builder& set_contains_callback(std::function<bool(std::string_view)> p_callback) {
            contains_callback = p_callback;
            return *this;
        }

        Builder& set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)> p_callback) {
            reserve_res_unit_storage_callback = p_callback;
            return *this;
        }

        Builder& set_register_res_unit_destructor_callback(std::function<void(void*, void (*)(void*))> p_callback) {
            register_res_unit_destructor_callback = p_callback;
            return *this;
        }

        Builder& set_create_ref_to_callback(std::function<void(std::function<void(WBE::RefRes<char>)>, std::string_view)> p_callback) {
            create_ref_to_callback = p_callback;
            return *this;
        }

        std::unique_ptr<MockResourceChunk> build(std::ostream& p_os) {
            return std::make_unique<MockResourceChunk>(get_resource_callback,
                try_get_resource_callback,
                get_size_callback,
                push_data_callback,
                get_data_callback,
                get_data_const_callback,
                get_all_keys_callback,
                contains_callback,
                reserve_res_unit_storage_callback,
                register_res_unit_destructor_callback,
                create_ref_to_callback,
                p_os);
        }
    };

    MockResourceChunk( // NOLINT
        std::function<WBE::RefRes<char>(std::string_view)> p_get_resource_cb,
        std::function<WBE::RefRes<char>(std::string_view)> p_try_get_resource_cb,
        std::function<size_t()> p_get_size_cb,
        std::function<WBE::RefRes<char>(size_t, const void*)> p_push_data_cb,
        std::function<void*(WBE::MemID)> p_get_data_cb,
        std::function<const void*(WBE::MemID)> p_get_data_const_cb,
        std::function<std::vector<std::string>()> p_get_all_keys_cb,
        std::function<bool(std::string_view)> p_contains_cb,
        std::function<WBE::RefRes<char>(std::string_view, size_t)> p_reserve_res_unit_storage_cb,
        std::function<void(void*, void (*)(void*))> p_register_res_unit_destructor_cb,
        std::function<void(std::function<void(WBE::RefRes<char>)>, std::string_view)> p_create_ref_to_cb,
        std::ostream& p_os)
        : get_resource_callback(p_get_resource_cb), try_get_resource_callback(p_try_get_resource_cb),
          get_size_callback(p_get_size_cb), push_data_callback(p_push_data_cb),
          get_data_callback(p_get_data_cb), get_data_const_callback(p_get_data_const_cb), get_all_keys_callback(p_get_all_keys_cb),
          contains_callback(p_contains_cb), reserve_res_unit_storage_callback(p_reserve_res_unit_storage_cb),
          register_res_unit_destructor_callback(p_register_res_unit_destructor_cb),
          create_ref_to_callback(p_create_ref_to_cb), os(&p_os) {
    }

    virtual WBE::RefRes<char> get_resource(std::string_view p_id) const override {
        WBE::RefRes<char> ret = WBE::MEM_NULL;
        if (get_resource_callback) {
            ret = get_resource_callback(p_id);
        }
        (*os) << "get_resource " << p_id << '\n';
        return ret;
    }

    virtual WBE::RefRes<char> try_get_resource(std::string_view p_id) const override {
        WBE::RefRes<char> ret = WBE::MEM_NULL;
        if (try_get_resource_callback) {
            ret = try_get_resource_callback(p_id);
        }
        (*os) << "try_get_resource " << p_id << '\n';
        return ret;
    }

    virtual size_t get_size() const override {
        size_t ret = 0;
        if (get_size_callback) {
            ret = get_size_callback();
        }
        (*os) << "get_size" << '\n';
        return ret;
    }

    virtual WBE::RefRes<char> push_data(size_t p_res_size, const void* p_res) override {
        WBE::RefRes<char> ret = WBE::MEM_NULL;
        if (push_data_callback) {
            ret = push_data_callback(p_res_size, p_res);
        }
        (*os) << "push_data " << p_res_size << " p_res" << '\n';
        return ret;
    }

    virtual void* get_data(WBE::MemID p_mem_id) override {
        void* ret = nullptr;
        if (get_data_callback) {
            ret = get_data_callback(p_mem_id);
        }
        (*os) << "get_data " << p_mem_id << '\n';
        return ret;
    }

    virtual const void* get_data(WBE::MemID p_mem_id) const override {
        const void* ret = nullptr;
        if (get_data_const_callback) {
            ret = get_data_const_callback(p_mem_id);
        }
        (*os) << "get_data " << p_mem_id << '\n';
        return ret;
    }

    virtual std::vector<std::string> get_all_keys() const override {
        std::vector<std::string> ret = {};
        if (get_all_keys_callback) {
            ret = get_all_keys_callback();
        }
        (*os) << "get_all_keys" << '\n';
        return ret;
    }

    virtual bool contains(std::string_view p_res_key) const override {
        bool ret = false;
        if (contains_callback) {
            ret = contains_callback(p_res_key);
        }
        (*os) << "contains " << p_res_key << '\n';
        return ret;
    }

protected:
    virtual void create_ref_to_resolver(std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) override {
        if (create_ref_to_callback) {
            create_ref_to_callback(std::move(p_assign_ref), p_key);
        }
        (*os) << "create_ref_to p_ptr " << p_key << '\n';
    }

    virtual WBE::IResourceChunk::ResUnitStorage reserve_res_unit_storage(std::string_view p_res_key, size_t p_size) override {
        WBE::RefRes<char> ret = WBE::MEM_NULL;
        if (reserve_res_unit_storage_callback) {
            ret = reserve_res_unit_storage_callback(p_res_key, p_size);
        }
        const char* resource_key = nullptr;
        if (!ret.is_null()) {
            auto resource = ret.force_cast<WBE::ResourceUnit>();
            resource->resource_key = nullptr;
            if (!p_res_key.empty()) {
                resource_keys.emplace_back(p_res_key);
                resource_key = resource_keys.back().c_str();
                resource->resource_key = resource_key;
            }
        }
        (*os) << "reserve_res_unit_storage " << p_res_key << " " << p_size << '\n';
        return WBE::IResourceChunk::ResUnitStorage{.resource = ret, .resource_key = resource_key};
    }

    virtual void register_res_unit_destructor(void* p_resource, void (*p_destructor)(void*)) override {
        if (register_res_unit_destructor_callback) {
            register_res_unit_destructor_callback(p_resource, p_destructor);
        }
        (*os) << "register_res_unit_destructor " << p_resource << '\n';
    }

private:
    std::function<WBE::RefRes<char>(std::string_view)> get_resource_callback;
    std::function<WBE::RefRes<char>(std::string_view)> try_get_resource_callback;
    std::function<size_t()> get_size_callback;
    std::function<WBE::RefRes<char>(size_t, const void*)> push_data_callback;
    std::function<void*(WBE::MemID)> get_data_callback;
    std::function<const void*(WBE::MemID)> get_data_const_callback;
    std::function<std::vector<std::string>()> get_all_keys_callback;
    std::function<bool(std::string_view)> contains_callback;
    std::function<WBE::RefRes<char>(std::string_view, size_t)> reserve_res_unit_storage_callback;
    std::function<void(void*, void (*)(void*))> register_res_unit_destructor_callback;
    std::function<void(std::function<void(WBE::RefRes<char>)>, std::string_view)> create_ref_to_callback;
    std::deque<std::string> resource_keys;
    std::ostream* os;
};

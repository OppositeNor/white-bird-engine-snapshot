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
#ifndef WBE_FILE_RESOURCE_POPULATOR_TEST_HH
#define WBE_FILE_RESOURCE_POPULATOR_TEST_HH

#include "core/allocator/i_allocator.hh"
#include "core/allocator/stack_allocator.hh"
#include "core/engine_core.hh"
#include "core/parser/parser_json.hh"
#include "function/render/render_resource_unit.hh"
#include "function/render_vk/render_vk_resource_unit_loader.hh"
#include "function/scene/scene_resource_unit.hh"
#include "function/scene/scene_resource_unit_loader.hh"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_int4.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "glm/ext/vector_uint4.hpp"
#include "global/global.hh"
#include "resource/ktx_texture.hh"
#include "resource/resource_chunk/resource_chunk_load_file.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_unit_loader/resource_unit_loader.hh"
#include "resource/resource_utils.hh"
#include "resource_test_utils.hh"
#include "test_global.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include "core/script/sb_lua.hh"
#include <gtest/gtest.h>
#include <memory>
#include <numbers>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

namespace ResourceUnitLoaderTestSupport {

struct TrivialTestResourceUnit : public WBE::ResourceUnit {
    TrivialTestResourceUnit() = default;

    explicit TrivialTestResourceUnit(int p_value) : value(p_value) {
    }

    int value = 0;
};

struct NonTrivialTestResourceUnit : public WBE::ResourceUnit {
    NonTrivialTestResourceUnit(int* p_construct_count, int* p_destroy_count, int p_value)
        : construct_count(p_construct_count), destroy_count(p_destroy_count), value(p_value) {
        if (construct_count != nullptr) {
            ++(*construct_count);
        }
    }

    ~NonTrivialTestResourceUnit() {
        if (destroy_count != nullptr) {
            ++(*destroy_count);
        }
    }

    NonTrivialTestResourceUnit(const NonTrivialTestResourceUnit&) = default;
    NonTrivialTestResourceUnit(NonTrivialTestResourceUnit&&) noexcept = default;
    NonTrivialTestResourceUnit& operator=(const NonTrivialTestResourceUnit&) = default;
    NonTrivialTestResourceUnit& operator=(NonTrivialTestResourceUnit&&) noexcept = default;

    int* construct_count = nullptr;
    int* destroy_count = nullptr;
    int value = 0;
};

struct OrderedDestroyTestResourceUnit : public WBE::ResourceUnit {
    OrderedDestroyTestResourceUnit(std::vector<int>* p_destroy_order, int p_id) : destroy_order(p_destroy_order), id(p_id) {
    }

    ~OrderedDestroyTestResourceUnit() {
        if (destroy_order != nullptr) {
            destroy_order->push_back(id);
        }
    }

    OrderedDestroyTestResourceUnit(const OrderedDestroyTestResourceUnit&) = default;
    OrderedDestroyTestResourceUnit(OrderedDestroyTestResourceUnit&&) noexcept = default;
    OrderedDestroyTestResourceUnit& operator=(const OrderedDestroyTestResourceUnit&) = default;
    OrderedDestroyTestResourceUnit& operator=(OrderedDestroyTestResourceUnit&&) noexcept = default;

    std::vector<int>* destroy_order = nullptr;
    int id = -1;
};

} // namespace ResourceUnitLoaderTestSupport

class ResourcePopulatorTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
        text_scheme.path = "test_text_files/test_file_utf8.txt";
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<MockResourceChunk> create_submesh_test_chunk(
        WBE::StackAllocator& p_stack_allocator, std::ostream& p_stream, WBE::RefRes<WBE::RUMesh3D>& p_captured_mesh) {
        return MockResourceChunk::Builder()
            .set_push_data_callback([&p_stack_allocator](size_t p_size, const void* p_data) -> WBE::RefRes<char> {
                if (p_size == 0) {
                    return WBE::MEM_NULL;
                }
                WBE::MemID mem_id = p_stack_allocator.allocate(p_size);
                std::memcpy(p_stack_allocator.get(mem_id), p_data, p_size);
                return WBE::RefRes<char>(mem_id, &p_stack_allocator);
            })
            .set_reserve_res_unit_storage_callback(
                [&p_stack_allocator, &p_captured_mesh](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = p_stack_allocator.allocate(p_size);
                    WBE::RefRes<char> resource(mem_id, &p_stack_allocator);
                    if (p_id == "loaded_mesh") {
                        p_captured_mesh = resource.force_cast<WBE::RUMesh3D>();
                    }
                    return resource;
                })
            .set_create_ref_to_callback([&p_stack_allocator](std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                if (p_key != "loaded_geometry") {
                    return;
                }
                constexpr size_t TEST_BINARY_SIZE = 256;
                WBE::MemID data_mem_id = p_stack_allocator.allocate(TEST_BINARY_SIZE);
                auto* data = static_cast<char*>(p_stack_allocator.get(data_mem_id));
                std::memset(data, 0, TEST_BINARY_SIZE);
                const std::array<float, 15> vertices{
                    0.0F,
                    0.5F,
                    0.0F,
                    0.5F,
                    1.0F,
                    -0.5F,
                    -0.5F,
                    0.0F,
                    0.0F,
                    0.0F,
                    0.5F,
                    -0.5F,
                    0.0F,
                    1.0F,
                    0.0F,
                };
                const std::array<uint32_t, 3> indices{0, 1, 2};
                std::memcpy(data, vertices.data(), sizeof(vertices));
                std::memcpy(data + sizeof(vertices), indices.data(), sizeof(indices));

                WBE::MemID binary_mem_id = p_stack_allocator.allocate(sizeof(WBE::RUBinary));
                auto* binary = static_cast<WBE::RUBinary*>(p_stack_allocator.get(binary_mem_id));
                std::construct_at(binary);
                binary->data = WBE::RefRes<char>(data_mem_id, &p_stack_allocator);
                binary->size = TEST_BINARY_SIZE;
                p_assign_ref(WBE::RefRes<char>(binary_mem_id, &p_stack_allocator));
            })
            .build(p_stream);
    }

    // Load p_scheme with the audio loader, capturing the pushed audio data into p_data and returning a copy of the unit.
    WBE::RUAudio load_audio(const WBE::RUSchemaAudio& p_scheme, std::vector<char>& p_data) {
        WBE::ResourceUnitLoaderAudio loader;
        std::stringstream stream;
        WBE::StackAllocator stack_allocator(1024);
        WBE::RefRes<WBE::RUAudio> captured_audio = WBE::MEM_NULL;
        auto res_chunk = MockResourceChunk::Builder()
                             .set_push_data_callback([&p_data](size_t p_size, const void* p_res) -> WBE::RefRes<char> {
                                 p_data.resize(p_size);
                                 std::memcpy(p_data.data(), p_res, p_size);
                                 return WBE::MEM_NULL;
                             })
                             .set_reserve_res_unit_storage_callback(
                                 [&captured_audio, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                     EXPECT_EQ(p_size, sizeof(WBE::RUAudio));
                                     WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                     captured_audio = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUAudio>();
                                     return captured_audio.force_cast<char>();
                                 })
                             .build(stream);
        loader.populate_scheme(*res_chunk, p_scheme);
        if (captured_audio == nullptr) {
            throw std::runtime_error("Audio loader did not store a resource unit.");
        }
        return *captured_audio;
    }

    static WBE::RUSchemaAudio make_audio_scheme(std::string_view p_path, bool p_is_stream) {
        WBE::RUSchemaAudio scheme;
        scheme.id = "loaded_audio";
        scheme.type = "audio";
        scheme.path = p_path;
        scheme.is_stream = p_is_stream;
        return scheme;
    }

    static float read_audio_sample(const std::vector<char>& p_data, size_t p_index) {
        float sample = 0.0F;
        std::memcpy(&sample, p_data.data() + (p_index * sizeof(float)), sizeof(float));
        return sample;
    }

    WBE::RUSchemaSubmesh3D make_triangle_submesh_scheme() {
        WBE::RUSchemaSubmesh3D scheme;
        scheme.material_id = "unused_material";
        scheme.aabb = {.center = {0.0F, 0.0F, 0.0F}, .width = 1.0F, .height = 1.0F, .depth = 1.0F};
        scheme.vertex_bindings.resize(1);
        scheme.vertex_bindings[0].binary = {.binary_id = "loaded_geometry", .start = 0, .size = sizeof(float) * 15};
        scheme.vertex_bindings[0].stride = sizeof(float) * 5;
        scheme.vertex_bindings[0].attributes = {
            {.role = "position", .type = "vec3", .offset = 0},
            {.role = "uv", .type = "vec2", .offset = sizeof(float) * 3},
        };
        scheme.indices.binary = {.binary_id = "loaded_geometry", .start = sizeof(float) * 15, .size = sizeof(uint32_t) * 3};
        scheme.first_instance = 0;
        scheme.instance_count = 1;
        return scheme;
    }

    static constexpr size_t JOINT_BINDING_STRIDE = sizeof(int32_t) * 4 + sizeof(float) * 4;

    static WBE::RUSchemaVertexView make_triangle_joint_bindings_scheme() {
        return {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = JOINT_BINDING_STRIDE * 3},
            .stride = JOINT_BINDING_STRIDE,
            .attributes =
                {
                    {.role = "joint_binding_ids", .type = "ivec4", .offset = 0},
                    {.role = "joint_binding_weights", .type = "vec4", .offset = sizeof(int32_t) * 4},
                },
        };
    }

    WBE::RUSchemaMesh3D make_single_submesh_mesh_scheme(WBE::RUSchemaSubmesh3D p_submesh) {
        WBE::RUSchemaMesh3D scheme;
        scheme.id = "loaded_mesh";
        scheme.type = "mesh";
        scheme.opaque_instances.submeshes = {std::move(p_submesh)};
        scheme.opaque_instances.instances = {WBE::RUSchemaInstance3D{}};
        return scheme;
    }

    template <typename T>
    WBE::RefRes<T> push_test_object(WBE::StackAllocator& p_stack_allocator, const T& p_value) {
        WBE::MemID mem_id = p_stack_allocator.allocate(sizeof(T));
        auto* object = static_cast<T*>(p_stack_allocator.get(mem_id));
        std::construct_at(object, p_value);
        return WBE::RefRes<T>(mem_id, &p_stack_allocator);
    }

    // Pack joints in the skeleton binary layout after p_prefix_size zero bytes.
    static std::vector<char> encode_skeleton_joints(const std::vector<WBE::RUSkeleton::Joint>& p_joints, size_t p_prefix_size) {
        std::vector<char> result(p_prefix_size + (p_joints.size() * WBE::RUSkeleton::JOINT_BYTE_SIZE));
        char* joint_data = result.data() + p_prefix_size;
        for (const WBE::RUSkeleton::Joint& joint : p_joints) {
            std::memcpy(joint_data, &joint.parent_index, sizeof(joint.parent_index));
            std::memcpy(joint_data + sizeof(joint.parent_index), &joint.bind_local_transform[0][0], 16 * sizeof(float));
            joint_data += WBE::RUSkeleton::JOINT_BYTE_SIZE;
        }
        return result;
    }

    WBE::RefRes<WBE::RUBinary> push_test_binary(WBE::StackAllocator& p_stack_allocator, const std::vector<char>& p_data) {
        WBE::MemID data_mem_id = p_stack_allocator.allocate(p_data.size());
        std::memcpy(p_stack_allocator.get(data_mem_id), p_data.data(), p_data.size());
        WBE::RUBinary binary;
        binary.data = WBE::RefRes<char>(data_mem_id, &p_stack_allocator);
        binary.size = p_data.size();
        return push_test_object(p_stack_allocator, binary);
    }

    // Load a skeleton scheme whose joints reference p_binary under the id "loaded_skeleton_joints".
    WBE::RefRes<WBE::RUSkeleton> load_test_skeleton(
        WBE::StackAllocator& p_stack_allocator, const WBE::RUSchemaSkeleton& p_scheme, WBE::RefRes<WBE::RUBinary> p_binary) {
        std::stringstream stream;
        WBE::RefRes<WBE::RUSkeleton> captured_skeleton = WBE::MEM_NULL;
        auto resource_chunk =
            MockResourceChunk::Builder()
                .set_reserve_res_unit_storage_callback(
                    [&p_stack_allocator, &captured_skeleton](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                        WBE::RefRes<char> resource(p_stack_allocator.allocate(p_size), &p_stack_allocator);
                        captured_skeleton = resource.force_cast<WBE::RUSkeleton>();
                        return resource;
                    })
                .set_create_ref_to_callback([&p_binary](std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                    if (p_key == "loaded_skeleton_joints") {
                        p_assign_ref(p_binary.force_cast<char>());
                    }
                })
                .build(stream);
        WBE::ResourceUnitLoaderSkeleton loader;
        loader.populate_scheme(*resource_chunk, p_scheme);
        return captured_skeleton;
    }

    static WBE::RUSchemaSkeleton make_skeleton_scheme(size_t p_start, size_t p_size) {
        WBE::RUSchemaSkeleton scheme;
        scheme.id = "loaded_skeleton";
        scheme.type = "skeleton";
        scheme.joints = {.binary_id = "loaded_skeleton_joints", .start = p_start, .size = p_size};
        return scheme;
    }

    std::unique_ptr<MockResourceChunk> create_static_mesh_test_chunk(
        WBE::StackAllocator& p_stack_allocator, std::ostream& p_stream, WBE::RefRes<WBE::RUMesh3D>& p_captured_set) {
        return MockResourceChunk::Builder()
            .set_push_data_callback([&p_stack_allocator](size_t p_size, const void* p_data) -> WBE::RefRes<char> {
                if (p_size == 0) {
                    return WBE::MEM_NULL;
                }
                WBE::MemID mem_id = p_stack_allocator.allocate(p_size);
                std::memcpy(p_stack_allocator.get(mem_id), p_data, p_size);
                return WBE::RefRes<char>(mem_id, &p_stack_allocator);
            })
            .set_reserve_res_unit_storage_callback(
                [&p_stack_allocator, &p_captured_set](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = p_stack_allocator.allocate(p_size);
                    WBE::RefRes<char> resource(mem_id, &p_stack_allocator);
                    if (p_id == "loaded_static_mesh") {
                        p_captured_set = resource.force_cast<WBE::RUMesh3D>();
                    }
                    return resource;
                })
            .set_create_ref_to_callback(
                [this, &p_stack_allocator](std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                    if (p_key == "loaded_geometry") {
                        std::array<uint8_t, 64> data{};
                        WBE::RUBinary binary;
                        binary.data = push_test_object(p_stack_allocator, data).force_cast<char>();
                        binary.size = data.size();
                        p_assign_ref(push_test_object(p_stack_allocator, binary).force_cast<char>());
                        return;
                    }
                    if (p_key == "loaded_material") {
                        WBE::RUMaterial material;
                        p_assign_ref(push_test_object(p_stack_allocator, material).force_cast<char>());
                    }
                })
            .build(p_stream);
    }

    std::unique_ptr<MockResourceChunk> create_split_static_mesh_test_chunk(
        WBE::StackAllocator& p_stack_allocator, std::ostream& p_stream, WBE::RefRes<WBE::RUMesh3D>& p_captured_set) {
        return MockResourceChunk::Builder()
            .set_push_data_callback([&p_stack_allocator](size_t p_size, const void* p_data) -> WBE::RefRes<char> {
                if (p_size == 0) {
                    return WBE::MEM_NULL;
                }
                WBE::MemID mem_id = p_stack_allocator.allocate(p_size);
                std::memcpy(p_stack_allocator.get(mem_id), p_data, p_size);
                return WBE::RefRes<char>(mem_id, &p_stack_allocator);
            })
            .set_reserve_res_unit_storage_callback(
                [&p_stack_allocator, &p_captured_set](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = p_stack_allocator.allocate(p_size);
                    WBE::RefRes<char> resource(mem_id, &p_stack_allocator);
                    if (p_id == "loaded_static_mesh") {
                        p_captured_set = resource.force_cast<WBE::RUMesh3D>();
                    }
                    return resource;
                })
            .set_create_ref_to_callback(
                [this, &p_stack_allocator](std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                    if (p_key == "loaded_vertices") {
                        std::array<uint8_t, sizeof(float) * 9> data{};
                        WBE::RUBinary binary;
                        binary.data = push_test_object(p_stack_allocator, data).force_cast<char>();
                        binary.size = data.size();
                        p_assign_ref(push_test_object(p_stack_allocator, binary).force_cast<char>());
                        return;
                    }
                    if (p_key == "loaded_indices") {
                        std::array<uint8_t, sizeof(uint32_t) * 3> data{};
                        WBE::RUBinary binary;
                        binary.data = push_test_object(p_stack_allocator, data).force_cast<char>();
                        binary.size = data.size();
                        p_assign_ref(push_test_object(p_stack_allocator, binary).force_cast<char>());
                        return;
                    }
                    if (p_key == "loaded_material") {
                        WBE::RUMaterial material;
                        p_assign_ref(push_test_object(p_stack_allocator, material).force_cast<char>());
                    }
                })
            .build(p_stream);
    }

    WBE::RUSchemaSubmesh3D make_static_geometry_submesh_scheme(
        std::string_view p_vertex_binary_id = "loaded_geometry", std::string_view p_index_binary_id = "loaded_geometry") {
        WBE::RUSchemaSubmesh3D scheme;
        scheme.material_id = "loaded_material";
        scheme.vertex_bindings.resize(1);
        scheme.vertex_bindings[0].binary = {.binary_id = std::string(p_vertex_binary_id), .start = 0, .size = sizeof(float) * 9};
        scheme.vertex_bindings[0].stride = sizeof(float) * 3;
        scheme.vertex_bindings[0].attributes = {{.role = "position", .type = "vec3", .offset = 0}};
        scheme.indices.binary = {.binary_id = std::string(p_index_binary_id), .start = 0, .size = sizeof(uint32_t) * 3};
        scheme.first_instance = 1;
        scheme.instance_count = 2;
        return scheme;
    }

    std::unique_ptr<WBE::Global> global;
    WBE::RUSchemaText text_scheme;
    std::string test_file_content = R"txt(这是一个测试文本。
English text is also supported.
日本語テキストもサポート。
한국어 텍스트도 지원됩니다.
Текст на русском тоже поддерживается.
Le texte français est également pris en charge.
El texto en español también está soportado.
Der deutsche Text wird ebenfalls unterstützt.
Il testo in italiano è supportato.
Português também é suportado.
النص العربي مدعوم أيضًا.
טקסט בעברית נתמך גם כן.
हिंदी पाठ भी समर्थित है।
ภาษาไทยก็รองรับเช่นกัน。
ქართული ტექსტიც მხარდაჭერილია.
Ελληνικό κείμενο υποστηρίζεται επίσης.
Türkçe metin de desteklenmektedir.
Український текст також підтримується.
Tiếng Việt cũng được hỗ trợ.
Emoji 测试: 😃🚀✨🌍
数学字符: ∀x∈ℝ, x² ≥ 0
组合字符: á é í ó ú
)txt";
};

class RefResHolder {
public:
    RefResHolder() : mem_id(WBE::global_allocator()->allocate(WBE_KI_B(4))), res(mem_id, WBE::global_allocator()) {
    }
    ~RefResHolder() {
        WBE::global_allocator()->deallocate(mem_id);
    }
    RefResHolder(const RefResHolder&) = delete;
    RefResHolder(RefResHolder&&) = delete;
    RefResHolder& operator=(const RefResHolder&) = delete;
    RefResHolder& operator=(RefResHolder&&) = delete;
    WBE::MemID mem_id;
    WBE::RefRes<char> res;
};

TEST_F(ResourcePopulatorTest, SupportedResourceTypesUseStaticReadOnlyStorage) {
    WBE::ResourceUnitLoaderAudio audio_loader;
    WBE::ResourceUnitLoaderBinary binary_loader;
    WBE::ResourceUnitLoaderFont font_loader;
    WBE::ResourceUnitLoaderImage image_loader;
    WBE::ResourceUnitLoaderSkeleton skeleton_loader;
    WBE::ResourceUnitLoaderTexture texture_loader;
    WBE::ResourceUnitLoaderText text_loader;
    WBE::ResourceUnitLoaderVideo video_loader;
    WBE::ResourceUnitLoaderShader shader_loader;
    WBE::ResourceUnitLoaderGraphicsPipeline graphics_pipeline_loader;
    WBE::ResourceUnitLoaderRayTracingPipeline ray_tracing_pipeline_loader;
    WBE::ResourceUnitLoaderMaterial material_loader;
    WBE::ResourceUnitLoaderMesh mesh_loader;
    WBE::ResourceUnitLoaderComponent component_loader;
    WBE::ResourceUnitLoaderPrefab prefab_loader;
    WBE::ResourceUnitLoaderWorld world_loader;
    WBE::ResourceUnitLoaderScene scene_loader;
    const std::array<const WBE::IResourceUnitLoader*, 17> loaders = {
        &audio_loader,
        &binary_loader,
        &font_loader,
        &image_loader,
        &skeleton_loader,
        &texture_loader,
        &text_loader,
        &video_loader,
        &shader_loader,
        &graphics_pipeline_loader,
        &ray_tracing_pipeline_loader,
        &material_loader,
        &mesh_loader,
        &component_loader,
        &prefab_loader,
        &world_loader,
        &scene_loader,
    };
    const std::array<std::string_view, 17> expected_resource_types = {
        "audio",
        "binary",
        "font",
        "image",
        "skeleton",
        "texture",
        "text",
        "video",
        "shader",
        "graphics_pipeline",
        "ray_tracing_pipeline",
        "material",
        "mesh",
        "component",
        "prefab",
        "world",
        "scene",
    };

    for (size_t i = 0; i < loaders.size(); ++i) {
        auto first_resource_types = loaders[i]->get_supported_resource_types();
        auto second_resource_types = loaders[i]->get_supported_resource_types();
        static_assert(std::is_const_v<decltype(first_resource_types)::element_type>);

        ASSERT_EQ(first_resource_types.size(), 1);
        EXPECT_EQ(first_resource_types.front(), expected_resource_types[i]);
        EXPECT_EQ(first_resource_types.data(), second_resource_types.data());
    }
}

TEST_F(ResourcePopulatorTest, RayTracingPipelinePopulatesFixedStagesAndOptionalEntries) {
    WBE::ResourceUnitLoaderRayTracingPipeline loader;
    WBE::StackAllocator stack_allocator(64 * 1024);
    std::stringstream stream;
    WBE::RefRes<WBE::RURayTracingPipeline> captured_pipeline;
    std::vector<std::string> created_ref_keys;

    auto push_to_stack = [&stack_allocator](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
        WBE::MemID mem_id = stack_allocator.allocate(p_res_size);
        std::memcpy(stack_allocator.get(mem_id), p_res, p_res_size);
        return WBE::RefRes<char>(mem_id, &stack_allocator);
    };
    WBE::RUShader raygen_shader;
    WBE::RUShader miss_shader;
    WBE::RUShader closest_hit_shader;
    WBE::RUShader any_hit_shader;
    WBE::RUShader intersection_shader;
    std::unordered_map<std::string, WBE::RefRes<char>> reference_resources{
        {"raygen_shader", push_to_stack(sizeof(raygen_shader), &raygen_shader)},
        {"miss_shader", push_to_stack(sizeof(miss_shader), &miss_shader)},
        {"closest_hit_shader", push_to_stack(sizeof(closest_hit_shader), &closest_hit_shader)},
        {"any_hit_shader", push_to_stack(sizeof(any_hit_shader), &any_hit_shader)},
        {"intersection_shader", push_to_stack(sizeof(intersection_shader), &intersection_shader)},
    };
    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(push_to_stack))
                         .set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)>(
                             [&captured_pipeline, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 WBE::RefRes<char> saved_resource(mem_id, &stack_allocator);
                                 captured_pipeline = saved_resource.force_cast<WBE::RURayTracingPipeline>();
                                 return saved_resource;
                             }))
                         .set_create_ref_to_callback([&created_ref_keys, &reference_resources](
                                                         std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                             created_ref_keys.emplace_back(p_key);
                             auto found_resource = reference_resources.find(created_ref_keys.back());
                             ASSERT_NE(found_resource, reference_resources.end());
                             p_assign_ref(found_resource->second);
                         })
                         .build(stream);

    constexpr std::string_view JSON_BUFFER = R"(
    {
        "id": "test_ray_tracing_pipeline",
        "type": "ray_tracing_pipeline",
        "raygen_stage_id": "raygen_shader",
        "raygen_stage_entry": "raygen_main",
        "miss_stage_id": "miss_shader",
        "miss_stage_entry": "miss_main",
        "closest_hit_stage_id": "closest_hit_shader",
        "closest_hit_stage_entry": "closest_hit_main",
        "any_hit_stage_id": "any_hit_shader",
        "any_hit_stage_entry": "any_hit_main",
        "intersection_shader_stage_id": "intersection_shader"
    }
    )";
    WBE::ParserJSON parser;
    parser.parse_from_buffer(JSON_BUFFER);

    loader.populate_scheme_from_data(*res_chunk, parser.get_data());

    ASSERT_NE(captured_pipeline, nullptr);
    EXPECT_EQ(captured_pipeline->raygen_stage.force_cast<char>(), reference_resources.at("raygen_shader"));
    EXPECT_EQ(captured_pipeline->miss_stage.force_cast<char>(), reference_resources.at("miss_shader"));
    EXPECT_EQ(captured_pipeline->closest_hit_stage.force_cast<char>(), reference_resources.at("closest_hit_shader"));
    EXPECT_EQ(captured_pipeline->any_hit_stage.force_cast<char>(), reference_resources.at("any_hit_shader"));
    EXPECT_EQ(captured_pipeline->intersection_shader_stage.force_cast<char>(), reference_resources.at("intersection_shader"));
    EXPECT_STREQ(captured_pipeline->raygen_stage_entry.get(), "raygen_main");
    EXPECT_STREQ(captured_pipeline->miss_stage_entry.get(), "miss_main");
    EXPECT_STREQ(captured_pipeline->closest_hit_stage_entry.get(), "closest_hit_main");
    EXPECT_STREQ(captured_pipeline->any_hit_stage_entry.get(), "any_hit_main");
    EXPECT_STREQ(captured_pipeline->intersection_shader_stage_entry.get(), "");
    EXPECT_EQ(created_ref_keys,
        (std::vector<std::string>{"raygen_shader", "miss_shader", "closest_hit_shader", "any_hit_shader", "intersection_shader"}));
}

TEST_F(ResourcePopulatorTest, TextureResourceOwnershipContract) {
    static_assert(!std::is_default_constructible_v<WBE::KTXTexture2>);
    static_assert(!std::is_copy_constructible_v<WBE::KTXTexture2>);
    static_assert(!std::is_copy_assignable_v<WBE::KTXTexture2>);
    static_assert(std::is_nothrow_move_constructible_v<WBE::KTXTexture2>);
    static_assert(std::is_nothrow_move_assignable_v<WBE::KTXTexture2>);
    static_assert(!std::is_default_constructible_v<WBE::RUTexture>);
    static_assert(!std::is_copy_constructible_v<WBE::RUTexture>);
    static_assert(std::is_nothrow_move_constructible_v<WBE::RUTexture>);

    EXPECT_THROW(WBE::KTXTexture2("res/assets/missing_texture.ktx2"), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, SubmeshGeometryViewsReferenceSharedBinaryData) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
    WBE::RUSchemaSubmesh3D scheme = make_triangle_submesh_scheme();
    WBE::ResourceUnitLoaderMesh loader;

    loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(scheme));

    ASSERT_NE(captured_mesh, nullptr);
    ASSERT_EQ(captured_mesh->opaque_instances.submesh_count, 1U);
    ASSERT_EQ(captured_mesh->opaque_instances.instance_count, 1U);
    EXPECT_EQ(captured_mesh->masked_instances.submesh_count, 0U);
    EXPECT_EQ(captured_mesh->double_sided_opaque_instances.submesh_count, 0U);
    WBE::RUSubmesh3D* captured_submesh = captured_mesh->opaque_instances.submeshes.get();
    EXPECT_EQ(captured_submesh->first_instance, 0U);
    EXPECT_EQ(captured_submesh->instance_count, 1U);
    EXPECT_EQ(captured_submesh->aabb.center, glm::vec3(0.0F));
    EXPECT_FLOAT_EQ(captured_submesh->aabb.width, 1.0F);
    EXPECT_FLOAT_EQ(captured_submesh->aabb.height, 1.0F);
    EXPECT_FLOAT_EQ(captured_submesh->aabb.depth, 1.0F);
    const WBE::RUVertexView& vertex_data = captured_submesh->get_vertex_bindings()[0];
    ASSERT_NE(vertex_data.binary.binary, nullptr);
    EXPECT_EQ(vertex_data.vertex_count, 3U);
    EXPECT_EQ(vertex_data.vertex_stride, sizeof(float) * 5);
    EXPECT_EQ(vertex_data.binary.start, 0U);
    EXPECT_EQ(vertex_data.binary.size, sizeof(float) * 15);
    ASSERT_EQ(vertex_data.attribute_count, 2U);
    ASSERT_NE(vertex_data.attributes, nullptr);
    EXPECT_STREQ(vertex_data.attributes->role.name.get(), "position");
    EXPECT_EQ(vertex_data.attributes->role.hash, WBE::dynam_hash("position"));
    EXPECT_EQ(vertex_data.attributes->format, WBE::RUVertexAttributeFormat::FLOAT3);
    EXPECT_EQ(vertex_data.attributes->offset, 0U);
    EXPECT_EQ(vertex_data.attributes->size, sizeof(float) * 3);
    EXPECT_STREQ(vertex_data.attributes.get()[1].role.name.get(), "uv");
    EXPECT_EQ(vertex_data.attributes.get()[1].role.hash, WBE::dynam_hash("uv"));
    EXPECT_EQ(vertex_data.attributes.get()[1].format, WBE::RUVertexAttributeFormat::FLOAT2);
    EXPECT_EQ(vertex_data.attributes.get()[1].offset, sizeof(float) * 3);
    EXPECT_EQ(vertex_data.attributes.get()[1].size, sizeof(float) * 2);

    const std::array<std::array<float, 5>, 3> expected_vertices{
        {
            {{0.0F, 0.5F, 0.0F, 0.5F, 1.0F}},
            {{-0.5F, -0.5F, 0.0F, 0.0F, 0.0F}},
            {{0.5F, -0.5F, 0.0F, 1.0F, 0.0F}},
        },
    };
    for (size_t vertex_index = 0; vertex_index < expected_vertices.size(); ++vertex_index) {
        std::array<float, 5> actual_vertex{};
        std::memcpy(actual_vertex.data(),
            vertex_data.binary.binary->data.get() + vertex_data.binary.start + vertex_index * vertex_data.vertex_stride,
            vertex_data.vertex_stride);
        for (size_t component_index = 0; component_index < actual_vertex.size(); ++component_index) {
            EXPECT_FLOAT_EQ(actual_vertex[component_index], expected_vertices[vertex_index][component_index]);
        }
    }

    ASSERT_EQ(captured_submesh->indices.index_count, 3U);
    ASSERT_NE(captured_submesh->indices.binary.binary, nullptr);
    const auto* indices = reinterpret_cast<const uint32_t*>(
        captured_submesh->indices.binary.binary->data.get() + captured_submesh->indices.binary.start);
    EXPECT_EQ(indices[0], 0U);
    EXPECT_EQ(indices[1], 1U);
    EXPECT_EQ(indices[2], 2U);
    EXPECT_EQ(captured_submesh->get_vertex_attribute_offset("uv"), sizeof(float) * 3);
    EXPECT_EQ(captured_submesh->get_vertex_bindings()[0].vertex_stride, sizeof(float) * 5);
    EXPECT_EQ(captured_submesh->get_vertices_num(), 3U);

    captured_submesh->set_vertex_attribute(1, "position", glm::vec3(2.0F, 3.0F, 4.0F));
    captured_submesh->set_vertex_attribute(1, "uv", glm::vec2(5.0F, 6.0F));
    std::array<float, 5> changed_vertex{};
    std::memcpy(changed_vertex.data(),
        vertex_data.binary.binary->data.get() + vertex_data.binary.start + vertex_data.vertex_stride,
        vertex_data.vertex_stride);
    EXPECT_EQ(changed_vertex, (std::array<float, 5>{{2.0F, 3.0F, 4.0F, 5.0F, 6.0F}}));
    EXPECT_THROW(captured_submesh->set_vertex_attribute(3, "position", glm::vec3(0.0F)), std::runtime_error);
    EXPECT_THROW(captured_submesh->set_vertex_attribute(0, "missing", glm::vec3(0.0F)), std::runtime_error);
    EXPECT_THROW(captured_submesh->set_vertex_attribute(0, "position", glm::vec2(0.0F)), std::runtime_error);

    captured_submesh->swap_vertices(0, 1);
    std::array<float, 5> first_vertex{};
    std::memcpy(first_vertex.data(), vertex_data.binary.binary->data.get() + vertex_data.binary.start, vertex_data.vertex_stride);
    EXPECT_EQ(first_vertex, changed_vertex);
    EXPECT_THROW(captured_submesh->swap_vertices(0, 3), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, SubmeshGeometrySupportsEveryVertexAttributeFormat) {
    struct FormatCase {
        std::string_view type;
        WBE::RUVertexAttributeFormat format;
        size_t element_size;
        size_t section_start;
    };
    const std::array<FormatCase, 12> format_cases{
        {
            {.type = "float", .format = WBE::RUVertexAttributeFormat::FLOAT, .element_size = 4, .section_start = 0},
            {.type = "vec2", .format = WBE::RUVertexAttributeFormat::FLOAT2, .element_size = 8, .section_start = 36},
            {.type = "vec3", .format = WBE::RUVertexAttributeFormat::FLOAT3, .element_size = 12, .section_start = 0},
            {.type = "vec4", .format = WBE::RUVertexAttributeFormat::FLOAT4, .element_size = 16, .section_start = 0},
            {.type = "int", .format = WBE::RUVertexAttributeFormat::INT, .element_size = 4, .section_start = 0},
            {.type = "ivec2", .format = WBE::RUVertexAttributeFormat::INT2, .element_size = 8, .section_start = 36},
            {.type = "ivec3", .format = WBE::RUVertexAttributeFormat::INT3, .element_size = 12, .section_start = 0},
            {.type = "ivec4", .format = WBE::RUVertexAttributeFormat::INT4, .element_size = 16, .section_start = 0},
            {.type = "uint", .format = WBE::RUVertexAttributeFormat::UINT, .element_size = 4, .section_start = 0},
            {.type = "uvec2", .format = WBE::RUVertexAttributeFormat::UINT2, .element_size = 8, .section_start = 36},
            {.type = "uvec3", .format = WBE::RUVertexAttributeFormat::UINT3, .element_size = 12, .section_start = 0},
            {.type = "uvec4", .format = WBE::RUVertexAttributeFormat::UINT4, .element_size = 16, .section_start = 0},
        },
    };
    WBE::ResourceUnitLoaderMesh loader;

    for (const FormatCase& format_case : format_cases) {
        WBE::StackAllocator stack_allocator(8192);
        std::stringstream stream;
        WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
        auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
        WBE::RUSchemaSubmesh3D scheme = make_triangle_submesh_scheme();
        scheme.vertex_bindings[0].binary.start = format_case.section_start;
        scheme.vertex_bindings[0].binary.size = format_case.element_size * 3;
        scheme.vertex_bindings[0].stride = format_case.element_size;
        scheme.vertex_bindings[0].attributes = {{.role = "custom_role", .type = std::string(format_case.type), .offset = 0}};

        loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(scheme));

        ASSERT_NE(captured_mesh, nullptr);
        WBE::RUSubmesh3D* captured_submesh = captured_mesh->opaque_instances.submeshes.get();
        ASSERT_EQ(captured_submesh->get_vertex_bindings()[0].attribute_count, 1U);
        EXPECT_EQ(captured_submesh->get_vertex_bindings()[0].attributes->format, format_case.format);
        EXPECT_EQ(captured_submesh->get_vertex_bindings()[0].attributes->size, format_case.element_size);
        EXPECT_EQ(captured_submesh->get_vertex_bindings()[0].vertex_stride, format_case.element_size);
        EXPECT_EQ(captured_submesh->get_vertex_bindings()[0].vertex_count, 3U);

        switch (format_case.format) {
        case WBE::RUVertexAttributeFormat::FLOAT:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", 1.0F));
            break;
        case WBE::RUVertexAttributeFormat::FLOAT2:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::vec2(1.0F)));
            break;
        case WBE::RUVertexAttributeFormat::FLOAT3:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::vec3(1.0F)));
            break;
        case WBE::RUVertexAttributeFormat::FLOAT4:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::vec4(1.0F)));
            break;
        case WBE::RUVertexAttributeFormat::INT:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", int32_t{1}));
            break;
        case WBE::RUVertexAttributeFormat::INT2:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::ivec2(1)));
            break;
        case WBE::RUVertexAttributeFormat::INT3:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::ivec3(1)));
            break;
        case WBE::RUVertexAttributeFormat::INT4:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::ivec4(1)));
            break;
        case WBE::RUVertexAttributeFormat::UINT:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", uint32_t{1}));
            break;
        case WBE::RUVertexAttributeFormat::UINT2:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::uvec2(1U)));
            break;
        case WBE::RUVertexAttributeFormat::UINT3:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::uvec3(1U)));
            break;
        case WBE::RUVertexAttributeFormat::UINT4:
            EXPECT_NO_THROW(captured_submesh->set_vertex_attribute(0, "custom_role", glm::uvec4(1U)));
            break;
        }
    }
}

TEST_F(ResourcePopulatorTest, SubmeshGeometryRejectsInvalidLayouts) {
    WBE::ResourceUnitLoaderMesh loader;
    auto expect_invalid = [this, &loader](WBE::RUSchemaVertexView p_vertices, WBE::RUSchemaIndexView p_indices) {
        WBE::StackAllocator stack_allocator(8192);
        std::stringstream stream;
        WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
        auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
        WBE::RUSchemaSubmesh3D scheme = make_triangle_submesh_scheme();
        scheme.vertex_bindings = {std::move(p_vertices)};
        scheme.indices = std::move(p_indices);
        EXPECT_THROW(loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(scheme)), std::runtime_error);
    };
    WBE::RUSchemaIndexView valid_indices{
        .binary = {.binary_id = "loaded_geometry", .start = sizeof(float) * 15, .size = sizeof(uint32_t) * 3},
    };

    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 60},
            .stride = 20,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}, {.role = "position", .type = "vec2", .offset = 12}},
        },
        valid_indices);
    static_assert(WBE::dynam_hash("role_002876ae4c05a223") == WBE::dynam_hash("role_2ffa43e2b7d24f56"));
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 72},
            .stride = 24,
            .attributes =
                {
                    {.role = "role_002876ae4c05a223", .type = "vec3", .offset = 0},
                    {.role = "role_2ffa43e2b7d24f56", .type = "vec3", .offset = 12},
                },
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 52},
            .stride = 20,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}, {.role = "uv", .type = "vec2", .offset = 12}},
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36},
            .stride = 12,
            .attributes = {{.role = "position", .type = "double", .offset = 0}},
        },
        valid_indices);
    expect_invalid({.binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36}, .stride = 12, .attributes = {}}, valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36},
            .stride = 0,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}},
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 2, .size = 36},
            .stride = 12,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}},
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36},
            .stride = 12,
            .attributes = {{.role = "position", .type = "vec3", .offset = 1}},
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "", .start = 0, .size = 36},
            .stride = 12,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}},
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36},
            .stride = 12,
            .attributes = {{.role = "", .type = "vec3", .offset = 0}},
        },
        valid_indices);
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36},
            .stride = 12,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}},
        },
        {.binary = {.binary_id = "loaded_geometry", .start = sizeof(float) * 15, .size = 10}});
    expect_invalid(
        {
            .binary = {.binary_id = "loaded_geometry", .start = 0, .size = 36},
            .stride = 12,
            .attributes = {{.role = "position", .type = "vec3", .offset = 0}},
        },
        {.binary = {.binary_id = "loaded_geometry", .start = sizeof(float) * 15, .size = sizeof(uint32_t) * 2}});
}

TEST_F(ResourcePopulatorTest, SubmeshAABBLoadsIntoSubmesh) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
    WBE::RUSchemaSubmesh3D scheme = make_triangle_submesh_scheme();
    scheme.aabb = {.center = {1.5F, -2.0F, 3.25F}, .width = 4.0F, .height = 5.5F, .depth = 6.5F};
    WBE::ResourceUnitLoaderMesh loader;

    loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(scheme));

    ASSERT_NE(captured_mesh, nullptr);
    ASSERT_EQ(captured_mesh->opaque_instances.submesh_count, 1U);
    const WBE::RUAABB3D& aabb = captured_mesh->opaque_instances.submeshes.get()[0].aabb;
    EXPECT_EQ(aabb.center, glm::vec3(1.5F, -2.0F, 3.25F));
    EXPECT_FLOAT_EQ(aabb.width, 4.0F);
    EXPECT_FLOAT_EQ(aabb.height, 5.5F);
    EXPECT_FLOAT_EQ(aabb.depth, 6.5F);
}

TEST_F(ResourcePopulatorTest, SubmeshAABBRejectsInvalidCenter) {
    WBE::ResourceUnitLoaderMesh loader;
    for (const std::vector<float>& center :
        {std::vector<float>{}, std::vector<float>{1.0F, 2.0F}, std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}}) {
        WBE::StackAllocator stack_allocator(8192);
        std::stringstream stream;
        WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
        auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
        WBE::RUSchemaSubmesh3D scheme = make_triangle_submesh_scheme();
        scheme.aabb.center = center;
        EXPECT_THROW(loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(scheme)), std::runtime_error);
    }
}

TEST_F(ResourcePopulatorTest, MeshLoadsEachCategorySubmeshViewsAndInstances) {
    using CategorySchema = WBE::RUSchemaSubmeshInstance3D WBE::RUSchemaMesh3D::*;
    using Category = WBE::RUSubmeshInstance3D WBE::RUMesh3D::*;
    const std::array<std::pair<CategorySchema, Category>, 3> categories{
        {
            {&WBE::RUSchemaMesh3D::opaque_instances, &WBE::RUMesh3D::opaque_instances},
            {&WBE::RUSchemaMesh3D::masked_instances, &WBE::RUMesh3D::masked_instances},
            {&WBE::RUSchemaMesh3D::double_sided_opaque_instances, &WBE::RUMesh3D::double_sided_opaque_instances},
        },
    };
    for (const auto& [schema_category, category] : categories) {
        WBE::StackAllocator stack_allocator(8192);
        std::stringstream stream;
        WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
        auto resource_chunk = create_static_mesh_test_chunk(stack_allocator, stream, captured_mesh);
        WBE::RUSchemaMesh3D scheme;
        scheme.id = "loaded_static_mesh";
        scheme.type = "mesh";
        (scheme.*schema_category).submeshes = {make_static_geometry_submesh_scheme()};
        (scheme.*schema_category).instances = {
            {.global_transform = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}},
            {.global_transform = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 2.0F, 3.0F, 4.0F, 1.0F}},
            {.global_transform = {2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 5.0F, 6.0F, 7.0F, 1.0F}},
        };
        WBE::ResourceUnitLoaderMesh loader;

        loader.populate_scheme(*resource_chunk, scheme);

        ASSERT_NE(captured_mesh, nullptr);
        for (const auto& [unused_schema_category, other_category] : categories) {
            static_cast<void>(unused_schema_category);
            if (other_category != category) {
                EXPECT_EQ(((*captured_mesh).*other_category).submesh_count, 0U);
                EXPECT_EQ(((*captured_mesh).*other_category).instance_count, 0U);
            }
        }
        const WBE::RUSubmeshInstance3D& loaded = (*captured_mesh).*category;
        ASSERT_EQ(loaded.submesh_count, 1U);
        ASSERT_NE(loaded.submeshes, nullptr);
        ASSERT_EQ(loaded.instance_count, 3U);
        ASSERT_NE(loaded.instances, nullptr);
        const WBE::RUSubmesh3D& submesh = loaded.submeshes.get()[0];
        ASSERT_NE(submesh.material, nullptr);
        ASSERT_NE(submesh.get_vertex_bindings()[0].binary.binary, nullptr);
        ASSERT_NE(submesh.indices.binary.binary, nullptr);
        EXPECT_EQ(submesh.get_vertex_bindings()[0].vertex_count, 3U);
        EXPECT_EQ(submesh.get_vertex_bindings()[0].vertex_stride, sizeof(float) * 3);
        ASSERT_EQ(submesh.get_vertex_bindings()[0].attribute_count, 1U);
        EXPECT_STREQ(submesh.get_vertex_bindings()[0].attributes->role.name.get(), "position");
        EXPECT_EQ(submesh.indices.index_count, 3U);
        EXPECT_EQ(submesh.first_instance, 1U);
        EXPECT_EQ(submesh.instance_count, 2U);
        EXPECT_EQ(loaded.instances.get()[1].global_transform[3][0], 2.0F);
        EXPECT_EQ(loaded.instances.get()[1].global_transform[3][1], 3.0F);
        EXPECT_EQ(loaded.instances.get()[1].global_transform[3][2], 4.0F);

        (scheme.*schema_category).submeshes[0].instance_count = 3;
        EXPECT_THROW(loader.populate_scheme(*resource_chunk, scheme), std::runtime_error);
    }
}

TEST_F(ResourcePopulatorTest, MeshLoadsSeparateVertexAndIndexBinaries) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    auto resource_chunk = create_split_static_mesh_test_chunk(stack_allocator, stream, captured_mesh);
    WBE::RUSchemaMesh3D scheme;
    scheme.id = "loaded_static_mesh";
    scheme.type = "mesh";
    scheme.opaque_instances.submeshes = {make_static_geometry_submesh_scheme("loaded_vertices", "loaded_indices")};
    scheme.opaque_instances.instances = {WBE::RUSchemaInstance3D{}, WBE::RUSchemaInstance3D{}, WBE::RUSchemaInstance3D{}};
    WBE::ResourceUnitLoaderMesh loader;

    loader.populate_scheme(*resource_chunk, scheme);

    ASSERT_NE(captured_mesh, nullptr);
    ASSERT_EQ(captured_mesh->opaque_instances.submesh_count, 1U);
    const WBE::RUSubmesh3D& submesh = captured_mesh->opaque_instances.submeshes.get()[0];
    ASSERT_NE(submesh.get_vertex_bindings()[0].binary.binary, nullptr);
    ASSERT_NE(submesh.indices.binary.binary, nullptr);
    EXPECT_NE(submesh.get_vertex_bindings()[0].binary.binary, submesh.indices.binary.binary);
    EXPECT_EQ(submesh.get_vertex_bindings()[0].binary.size, sizeof(float) * 9);
    EXPECT_EQ(submesh.indices.binary.size, sizeof(uint32_t) * 3);
    EXPECT_EQ(submesh.get_vertex_bindings()[0].binary.start, 0U);
    EXPECT_EQ(submesh.indices.binary.start, 0U);
}

TEST_F(ResourcePopulatorTest, MeshLoadsEmptyCategories) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    auto resource_chunk = MockResourceChunk::Builder()
                              .set_reserve_res_unit_storage_callback(
                                  [&stack_allocator, &captured_mesh](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                                      WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                      WBE::RefRes<char> resource(mem_id, &stack_allocator);
                                      if (p_id == "empty_mesh") {
                                          captured_mesh = resource.force_cast<WBE::RUMesh3D>();
                                      }
                                      return resource;
                                  })
                              .build(stream);
    WBE::RUSchemaMesh3D scheme;
    scheme.id = "empty_mesh";
    scheme.type = "mesh";
    WBE::ResourceUnitLoaderMesh loader;

    loader.populate_scheme(*resource_chunk, scheme);

    ASSERT_NE(captured_mesh, nullptr);
    for (const WBE::RUSubmeshInstance3D* category :
        {&captured_mesh->opaque_instances, &captured_mesh->masked_instances, &captured_mesh->double_sided_opaque_instances}) {
        EXPECT_EQ(category->submesh_count, 0U);
        EXPECT_EQ(category->instance_count, 0U);
        EXPECT_EQ(category->submeshes, nullptr);
        EXPECT_EQ(category->instances, nullptr);
    }
}

TEST_F(ResourcePopulatorTest, SkeletonLoadsJointTree) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUSkeleton> captured_skeleton = WBE::MEM_NULL;
    std::vector<std::string> created_ref_keys;
    glm::mat4 scaled(2.0F);
    scaled[3] = glm::vec4(0.0F, 1.0F, 0.0F, 1.0F);
    const std::vector<WBE::RUSkeleton::Joint> source_joints = {
        {.parent_index = -1, .bind_local_transform = glm::mat4(1.0F)},
        {.parent_index = 0, .bind_local_transform = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 1.0F, 0.0F))},
        {.parent_index = 1, .bind_local_transform = scaled},
        {.parent_index = 0, .bind_local_transform = glm::translate(glm::mat4(1.0F), glm::vec3(3.0F, 0.0F, 0.0F))},
    };
    constexpr size_t JOINTS_START = 8;
    WBE::RefRes<WBE::RUBinary> stored_binary = push_test_binary(stack_allocator, encode_skeleton_joints(source_joints, JOINTS_START));
    auto resource_chunk =
        MockResourceChunk::Builder()
            .set_reserve_res_unit_storage_callback(
                [&stack_allocator, &captured_skeleton](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = stack_allocator.allocate(p_size);
                    WBE::RefRes<char> resource(mem_id, &stack_allocator);
                    if (p_id == "loaded_skeleton") {
                        captured_skeleton = resource.force_cast<WBE::RUSkeleton>();
                    }
                    return resource;
                })
            .set_create_ref_to_callback(
                [&created_ref_keys, &stored_binary](std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                    created_ref_keys.emplace_back(p_key);
                    if (p_key == "loaded_skeleton_joints") {
                        p_assign_ref(stored_binary.force_cast<char>());
                    }
                })
            .build(stream);
    constexpr std::string_view JSON_BUFFER = R"(
    {
        "id": "loaded_skeleton",
        "type": "skeleton",
        "joints": {"binary_id": "loaded_skeleton_joints", "start": 8, "size": 272}
    }
    )";
    WBE::ParserJSON parser;
    parser.parse_from_buffer(JSON_BUFFER);
    WBE::ResourceUnitLoaderSkeleton loader;

    loader.populate_scheme_from_data(*resource_chunk, parser.get_data());

    ASSERT_NE(captured_skeleton, nullptr);
    EXPECT_EQ(created_ref_keys, std::vector<std::string>{"loaded_skeleton_joints"});
    ASSERT_EQ(captured_skeleton->joint_count, 4U);
    EXPECT_EQ(captured_skeleton->joints.binary, stored_binary);
    EXPECT_EQ(captured_skeleton->joints.start, JOINTS_START);
    EXPECT_EQ(captured_skeleton->joints.size, 4 * WBE::RUSkeleton::JOINT_BYTE_SIZE);
    for (size_t i = 0; i < source_joints.size(); ++i) {
        const WBE::RUSkeleton::Joint joint = captured_skeleton->get_joint(i);
        EXPECT_EQ(joint.parent_index, source_joints[i].parent_index);
        EXPECT_EQ(joint.bind_local_transform, source_joints[i].bind_local_transform);
    }
    EXPECT_EQ(captured_skeleton->get_joint(1).bind_local_transform[3], glm::vec4(0.0F, 1.0F, 0.0F, 1.0F));
    EXPECT_EQ(captured_skeleton->get_joint(2).bind_local_transform[0][0], 2.0F);
    EXPECT_EQ(captured_skeleton->get_joint(3).bind_local_transform[3][0], 3.0F);
    EXPECT_THROW(captured_skeleton->get_joint(4), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, SkeletonRejectsInvalidJointViews) {
    WBE::ResourceUnitLoaderSkeleton loader;
    auto expect_invalid = [&loader](const WBE::RUSchemaSkeleton& p_scheme) {
        WBE::StackAllocator stack_allocator(8192);
        std::stringstream stream;
        auto resource_chunk = MockResourceChunk::Builder()
                                  .set_reserve_res_unit_storage_callback([&stack_allocator](std::string_view, size_t p_size) {
                                      return WBE::RefRes<char>(stack_allocator.allocate(p_size), &stack_allocator);
                                  })
                                  .build(stream);
        EXPECT_THROW(loader.populate_scheme(*resource_chunk, p_scheme), std::runtime_error);
    };

    expect_invalid(make_skeleton_scheme(0, 0));
    expect_invalid(make_skeleton_scheme(0, WBE::RUSkeleton::JOINT_BYTE_SIZE + 1));
    WBE::RUSchemaSkeleton missing_binary = make_skeleton_scheme(0, WBE::RUSkeleton::JOINT_BYTE_SIZE);
    missing_binary.joints.binary_id.clear();
    expect_invalid(missing_binary);
    WBE::RUSchemaSkeleton nul_binary = make_skeleton_scheme(0, WBE::RUSkeleton::JOINT_BYTE_SIZE);
    nul_binary.joints.binary_id = std::string("loaded\0joints", 13);
    expect_invalid(nul_binary);
}

TEST_F(ResourcePopulatorTest, SkeletonRejectsInvalidJointTrees) {
    auto expect_invalid = [this](std::vector<int32_t> p_parent_indices, size_t p_invalid_joint_index) {
        WBE::StackAllocator stack_allocator(8192);
        std::vector<WBE::RUSkeleton::Joint> joints;
        for (int32_t parent_index : p_parent_indices) {
            joints.push_back({.parent_index = parent_index, .bind_local_transform = glm::mat4(1.0F)});
        }
        WBE::RefRes<WBE::RUBinary> binary = push_test_binary(stack_allocator, encode_skeleton_joints(joints, 0));
        WBE::RefRes<WBE::RUSkeleton> skeleton =
            load_test_skeleton(stack_allocator, make_skeleton_scheme(0, joints.size() * WBE::RUSkeleton::JOINT_BYTE_SIZE), binary);
        ASSERT_NE(skeleton, nullptr);
        for (size_t i = 0; i < p_invalid_joint_index; ++i) {
            EXPECT_NO_THROW(skeleton->get_joint(i));
        }
        EXPECT_THROW(skeleton->get_joint(p_invalid_joint_index), std::runtime_error);
    };

    expect_invalid({0}, 0);
    expect_invalid({-1, -1}, 1);
    expect_invalid({-1, -2}, 1);
    expect_invalid({-1, 1}, 1);
    expect_invalid({-1, 2, 0}, 1);
}

TEST_F(ResourcePopulatorTest, SkeletonRejectsJointRangeOutsideBinary) {
    WBE::StackAllocator stack_allocator(8192);
    const std::vector<WBE::RUSkeleton::Joint> joints = {{.parent_index = -1, .bind_local_transform = glm::mat4(1.0F)}};
    WBE::RefRes<WBE::RUBinary> binary = push_test_binary(stack_allocator, encode_skeleton_joints(joints, 0));

    WBE::RefRes<WBE::RUSkeleton> overflowing_skeleton =
        load_test_skeleton(stack_allocator, make_skeleton_scheme(4, WBE::RUSkeleton::JOINT_BYTE_SIZE), binary);
    ASSERT_NE(overflowing_skeleton, nullptr);
    EXPECT_THROW(overflowing_skeleton->get_joint(0), std::runtime_error);

    WBE::RefRes<WBE::RUSkeleton> unresolved_skeleton =
        load_test_skeleton(stack_allocator, make_skeleton_scheme(0, WBE::RUSkeleton::JOINT_BYTE_SIZE), WBE::MEM_NULL);
    ASSERT_NE(unresolved_skeleton, nullptr);
    EXPECT_THROW(unresolved_skeleton->get_joint(0), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, MeshLoadsSkeletonReferenceAndJointBindings) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    std::vector<std::string> created_ref_keys;
    WBE::RUSkeleton skeleton;
    skeleton.joint_count = 2;
    WBE::RefRes<WBE::RUSkeleton> stored_skeleton = push_test_object(stack_allocator, skeleton);
    std::array<uint8_t, 256> binary_data{};
    WBE::RUBinary binary;
    binary.data = push_test_object(stack_allocator, binary_data).force_cast<char>();
    binary.size = binary_data.size();
    WBE::RefRes<WBE::RUBinary> stored_binary = push_test_object(stack_allocator, binary);
    auto resource_chunk =
        MockResourceChunk::Builder()
            .set_push_data_callback([&stack_allocator](size_t p_size, const void* p_data) -> WBE::RefRes<char> {
                WBE::MemID mem_id = stack_allocator.allocate(p_size);
                std::memcpy(stack_allocator.get(mem_id), p_data, p_size);
                return WBE::RefRes<char>(mem_id, &stack_allocator);
            })
            .set_reserve_res_unit_storage_callback(
                [&stack_allocator, &captured_mesh](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = stack_allocator.allocate(p_size);
                    WBE::RefRes<char> resource(mem_id, &stack_allocator);
                    if (p_id == "loaded_mesh") {
                        captured_mesh = resource.force_cast<WBE::RUMesh3D>();
                    }
                    return resource;
                })
            .set_create_ref_to_callback([&created_ref_keys, &stored_skeleton, &stored_binary](
                                            std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                created_ref_keys.emplace_back(p_key);
                if (p_key == "loaded_skeleton") {
                    p_assign_ref(stored_skeleton.force_cast<char>());
                } else if (p_key == "loaded_geometry" || p_key == "loaded_joint_bindings") {
                    p_assign_ref(stored_binary.force_cast<char>());
                }
            })
            .build(stream);
    WBE::RUSchemaSubmesh3D submesh_scheme = make_triangle_submesh_scheme();
    submesh_scheme.vertex_bindings.push_back(make_triangle_joint_bindings_scheme());
    submesh_scheme.vertex_bindings[1].binary.binary_id = "loaded_joint_bindings";
    WBE::RUSchemaMesh3D scheme = make_single_submesh_mesh_scheme(submesh_scheme);
    scheme.skeleton_id = "loaded_skeleton";
    WBE::ResourceUnitLoaderMesh loader;

    loader.populate_scheme(*resource_chunk, scheme);

    ASSERT_NE(captured_mesh, nullptr);
    EXPECT_EQ(captured_mesh->skeleton, stored_skeleton);
    ASSERT_EQ(captured_mesh->opaque_instances.submesh_count, 1U);
    const WBE::RUSubmesh3D& submesh = captured_mesh->opaque_instances.submeshes.get()[0];
    ASSERT_EQ(submesh.vertex_binding_count, 2U);
    EXPECT_EQ(submesh.get_vertices_num(), 3U);
    EXPECT_EQ(submesh.get_vertex_binding_index("position"), 0U);
    EXPECT_EQ(submesh.get_vertex_binding_index("joint_binding_ids"), 1U);
    const WBE::RUVertexView& joint_bindings = submesh.get_vertex_bindings()[1];
    EXPECT_EQ(joint_bindings.binary.binary, stored_binary);
    EXPECT_EQ(joint_bindings.vertex_count, 3U);
    EXPECT_EQ(joint_bindings.vertex_stride, JOINT_BINDING_STRIDE);
    ASSERT_EQ(joint_bindings.attribute_count, 2U);
    EXPECT_STREQ(joint_bindings.attributes.get()[0].role.name.get(), "joint_binding_ids");
    EXPECT_EQ(joint_bindings.attributes.get()[0].format, WBE::RUVertexAttributeFormat::INT4);
    EXPECT_STREQ(joint_bindings.attributes.get()[1].role.name.get(), "joint_binding_weights");
    EXPECT_EQ(joint_bindings.attributes.get()[1].format, WBE::RUVertexAttributeFormat::FLOAT4);
    EXPECT_EQ(joint_bindings.get_vertex_attribute_offset("joint_binding_weights"), sizeof(int32_t) * 4);
    EXPECT_NE(std::ranges::find(created_ref_keys, "loaded_joint_bindings"), created_ref_keys.end());
    EXPECT_NE(std::ranges::find(created_ref_keys, "loaded_skeleton"), created_ref_keys.end());
}

TEST_F(ResourcePopulatorTest, MeshWithoutSkeletonHasNoSkeletonOrJointBindings) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
    WBE::ResourceUnitLoaderMesh loader;

    loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(make_triangle_submesh_scheme()));

    ASSERT_NE(captured_mesh, nullptr);
    EXPECT_EQ(captured_mesh->skeleton, nullptr);
    EXPECT_EQ(captured_mesh->opaque_instances.submeshes.get()[0].vertex_binding_count, 1U);
}

TEST_F(ResourcePopulatorTest, MeshRejectsInvalidSkeletonBindings) {
    WBE::ResourceUnitLoaderMesh loader;
    auto expect_invalid = [this, &loader](std::optional<std::string> p_skeleton_id, WBE::RUSchemaVertexView p_joint_bindings) {
        WBE::StackAllocator stack_allocator(8192);
        std::stringstream stream;
        WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
        auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
        WBE::RUSchemaSubmesh3D submesh_scheme = make_triangle_submesh_scheme();
        submesh_scheme.vertex_bindings.push_back(std::move(p_joint_bindings));
        WBE::RUSchemaMesh3D scheme = make_single_submesh_mesh_scheme(submesh_scheme);
        scheme.skeleton_id = std::move(p_skeleton_id);
        EXPECT_THROW(loader.populate_scheme(*resource_chunk, scheme), std::runtime_error);
    };
    const WBE::RUSchemaVertexView valid_joint_bindings = make_triangle_joint_bindings_scheme();

    expect_invalid(std::nullopt, valid_joint_bindings);
    expect_invalid(std::string(), valid_joint_bindings);

    WBE::RUSchemaVertexView missing_ids = valid_joint_bindings;
    missing_ids.attributes[0].role = "other_role";
    expect_invalid("loaded_skeleton", missing_ids);

    WBE::RUSchemaVertexView missing_weights = valid_joint_bindings;
    missing_weights.attributes[1].role = "other_role";
    expect_invalid("loaded_skeleton", missing_weights);

    WBE::RUSchemaVertexView unsigned_ids = valid_joint_bindings;
    unsigned_ids.attributes[0].type = "uvec4";
    expect_invalid("loaded_skeleton", unsigned_ids);

    WBE::RUSchemaVertexView scalar_weights = valid_joint_bindings;
    scalar_weights.attributes[1].type = "float";
    expect_invalid("loaded_skeleton", scalar_weights);

    WBE::RUSchemaVertexView too_few_bindings = valid_joint_bindings;
    too_few_bindings.binary.size = JOINT_BINDING_STRIDE * 2;
    expect_invalid("loaded_skeleton", too_few_bindings);

    WBE::RUSchemaVertexView duplicated_position = valid_joint_bindings;
    duplicated_position.attributes.push_back({.role = "position", .type = "vec3", .offset = JOINT_BINDING_STRIDE});
    duplicated_position.stride = JOINT_BINDING_STRIDE + sizeof(float) * 3;
    duplicated_position.binary.size = duplicated_position.stride * 3;
    expect_invalid("loaded_skeleton", duplicated_position);
}

TEST_F(ResourcePopulatorTest, MeshRejectsSubmeshWithoutVertexBindings) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMesh3D> captured_mesh = WBE::MEM_NULL;
    auto resource_chunk = create_submesh_test_chunk(stack_allocator, stream, captured_mesh);
    WBE::RUSchemaSubmesh3D submesh_scheme = make_triangle_submesh_scheme();
    submesh_scheme.vertex_bindings.clear();
    WBE::ResourceUnitLoaderMesh loader;
    EXPECT_THROW(loader.populate_scheme(*resource_chunk, make_single_submesh_mesh_scheme(submesh_scheme)), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, WorldLoadsStaticMeshRenderId) {
    WBE::StackAllocator stack_allocator(8192);
    std::stringstream stream;
    WBE::RefRes<WBE::RUWorld> captured_world = WBE::MEM_NULL;
    auto resource_chunk = MockResourceChunk::Builder()
                              .set_push_data_callback([&stack_allocator](size_t p_size, const void* p_data) -> WBE::RefRes<char> {
                                  WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                  std::memcpy(stack_allocator.get(mem_id), p_data, p_size);
                                  return WBE::RefRes<char>(mem_id, &stack_allocator);
                              })
                              .set_reserve_res_unit_storage_callback(
                                  [&stack_allocator, &captured_world](std::string_view p_id, size_t p_size) -> WBE::RefRes<char> {
                                      WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                      WBE::RefRes<char> resource(mem_id, &stack_allocator);
                                      if (p_id == "test_world") {
                                          captured_world = resource.force_cast<WBE::RUWorld>();
                                      }
                                      return resource;
                                  })
                              .build(stream);
    constexpr std::string_view JSON_BUFFER = R"(
        {
            "id": "test_world",
            "type": "world",
            "static_component": {
                "static_mesh_render": "model_cube_static.mesh"
            }
        }
    )";
    WBE::ParserJSON parser;
    parser.parse_from_buffer(JSON_BUFFER);
    WBE::ResourceUnitLoaderWorld loader;

    loader.populate_scheme_from_data(*resource_chunk, parser.get_data());

    ASSERT_NE(captured_world, nullptr);
    ASSERT_NE(captured_world->static_component.static_mesh_render, nullptr);
    EXPECT_STREQ(captured_world->static_component.static_mesh_render.get(), "model_cube_static.mesh");
}

TEST_F(ResourcePopulatorTest, WorldRejectsEmptyStaticMeshRenderId) {
    std::stringstream stream;
    auto resource_chunk = MockResourceChunk::Builder().build(stream);
    WBE::RUSchemaWorld scheme;
    scheme.id = "empty_world";
    scheme.type = "world";
    WBE::ResourceUnitLoaderWorld loader;

    EXPECT_THROW(loader.populate_scheme(*resource_chunk, scheme), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, EmplaceResUnitRegistersDestructorForNonTrivialResourceUnit) {
    std::stringstream stream;
    WBE::StackAllocator stack_allocator(1024);
    void* registered_resource = nullptr;
    void (*registered_destructor)(void*) = nullptr;
    int construct_count = 0;
    int destroy_count = 0;

    auto res_chunk =
        MockResourceChunk::Builder()
            .set_reserve_res_unit_storage_callback([&stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                WBE::MemID mem_id = stack_allocator.allocate(p_size);
                return WBE::RefRes<char>(mem_id, &stack_allocator);
            })
            .set_register_res_unit_destructor_callback(
                [&registered_resource, &registered_destructor](void* p_resource, void (*p_destructor)(void*)) {
                    registered_resource = p_resource;
                    registered_destructor = p_destructor;
                })
            .build(stream);

    auto resource = res_chunk->emplace_res_unit<ResourceUnitLoaderTestSupport::NonTrivialTestResourceUnit>(
        "typed_resource", &construct_count, &destroy_count, 17);

    ASSERT_NE(resource, nullptr);
    ASSERT_NE(resource->resource_key, nullptr);
    EXPECT_STREQ(resource->resource_key, "typed_resource");
    EXPECT_EQ(resource->value, 17);
    EXPECT_EQ(construct_count, 1);
    ASSERT_NE(registered_resource, nullptr);
    ASSERT_NE(registered_destructor, nullptr);
    EXPECT_EQ(registered_resource, resource.get());
    EXPECT_EQ(destroy_count, 0);

    registered_destructor(registered_resource);
    EXPECT_EQ(destroy_count, 1);
}

TEST_F(ResourcePopulatorTest, EmplaceResUnitSkipsDestructorRegistrationForTrivialResourceUnit) {
    std::stringstream stream;
    WBE::StackAllocator stack_allocator(1024);
    int register_destructor_count = 0;

    auto res_chunk =
        MockResourceChunk::Builder()
            .set_reserve_res_unit_storage_callback([&stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                WBE::MemID mem_id = stack_allocator.allocate(p_size);
                return WBE::RefRes<char>(mem_id, &stack_allocator);
            })
            .set_register_res_unit_destructor_callback([&register_destructor_count](void*, void (*)(void*)) {
                ++register_destructor_count;
            })
            .build(stream);

    auto resource = res_chunk->emplace_res_unit<ResourceUnitLoaderTestSupport::TrivialTestResourceUnit>("trivial_resource", 23);

    ASSERT_NE(resource, nullptr);
    ASSERT_NE(resource->resource_key, nullptr);
    EXPECT_STREQ(resource->resource_key, "trivial_resource");
    EXPECT_EQ(resource->value, 23);
    EXPECT_EQ(register_destructor_count, 0);
}

TEST_F(ResourcePopulatorTest, ResourceChunkLoadFileDestroysNonTrivialResourceUnitsInReverseOrder) {
    WBE::StackAllocator stack_allocator(64 * 1024);
    std::vector<int> destroy_order;

    {
        WBE::ResourceChunkLoadFile resource_chunk("res_chunks/empty_resource_chunk.json", stack_allocator);
        auto first_resource = resource_chunk.emplace_res_unit<ResourceUnitLoaderTestSupport::OrderedDestroyTestResourceUnit>(
            "destroy_order_first", &destroy_order, 1);
        auto second_resource = resource_chunk.emplace_res_unit<ResourceUnitLoaderTestSupport::OrderedDestroyTestResourceUnit>(
            "destroy_order_second", &destroy_order, 2);

        ASSERT_NE(first_resource, nullptr);
        ASSERT_NE(second_resource, nullptr);
        EXPECT_TRUE(destroy_order.empty());
    }

    ASSERT_EQ(destroy_order.size(), 2U);
    EXPECT_EQ(destroy_order[0], 2);
    EXPECT_EQ(destroy_order[1], 1);
}

TEST_F(ResourcePopulatorTest, TextResourceGeneral) {
    WBE::ResourceUnitLoaderText loader;
    std::stringstream ss;
    std::string text_content_storage;
    WBE::StackAllocator stack_allocator(1024);
    WBE::RefRes<WBE::RUText> captured_ru_text = WBE::MEM_NULL;
    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(
                             [&text_content_storage](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
                                 text_content_storage.resize(p_res_size - 1);
                                 std::memcpy(text_content_storage.data(), p_res, p_res_size - 1);
                                 EXPECT_EQ(static_cast<const char*>(p_res)[p_res_size - 1], '\0');
                                 return WBE::MEM_NULL;
                             }))
                         .set_reserve_res_unit_storage_callback(
                             [&captured_ru_text, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 EXPECT_EQ(p_size, sizeof(WBE::RUText));
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 captured_ru_text = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUText>();
                                 return captured_ru_text.force_cast<char>();
                             })
                         .build(ss);
    loader.populate_scheme(*res_chunk, text_scheme);
    ASSERT_NE(captured_ru_text, nullptr);
    EXPECT_EQ(captured_ru_text->size, test_file_content.size() + 1);
    EXPECT_EQ(text_content_storage, test_file_content);
}

TEST_F(ResourcePopulatorTest, ScriptResourceGeneral) {
    WBE::ResourceUnitLoaderScript loader;
    std::stringstream ss;
    std::string source_storage;
    WBE::StackAllocator stack_allocator(1024);
    WBE::RefRes<WBE::RUScript> captured_ru_script = WBE::MEM_NULL;
    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(
                             [&source_storage](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
                                 EXPECT_EQ(static_cast<const char*>(p_res)[p_res_size - 1], '\0');
                                 source_storage.assign(static_cast<const char*>(p_res), p_res_size - 1);
                                 return WBE::MEM_NULL;
                             }))
                         .set_reserve_res_unit_storage_callback(
                             [&captured_ru_script, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 EXPECT_EQ(p_size, sizeof(WBE::RUScript));
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 captured_ru_script = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUScript>();
                                 return captured_ru_script.force_cast<char>();
                             })
                         .build(ss);
    WBE::RUSchemaScript script_scheme;
    script_scheme.path = "test_scripts/test_script.lua";
    loader.populate_scheme(*res_chunk, script_scheme);
    ASSERT_NE(captured_ru_script, nullptr);
    EXPECT_EQ(captured_ru_script->size, source_storage.size() + 1);

    // The loaded source is a module a script backend can run.
    WBE::SBLua backend;
    backend.load_module("test_script", source_storage);
    EXPECT_EQ(backend.call("test_script", "answer", {}).as_integer(), 42);
}

TEST_F(ResourcePopulatorTest, ScriptSchemeRequiresPath) {
    WBE::ResourceUnitLoaderScript loader;
    std::stringstream ss;
    auto res_chunk = MockResourceChunk::Builder().build(ss);
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({"type": "script", "id": "missing_path"})");
    EXPECT_THROW(loader.populate_scheme_from_data(*res_chunk, parser.get_data()), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, BinaryReousrceGeneral) {
    WBE::ResourceUnitLoaderBinary loader;
    std::stringstream ss;
    WBE::StackAllocator stack_allocator(1024);
    std::vector<uint8_t> binary_data_storage;
    WBE::RefRes<WBE::RUBinary> captured_ru_binary = WBE::MEM_NULL;
    WBE::RUSchemaBinary binary_scheme;
    binary_scheme.path = "assets/meshes/triangle.submesh.0.geometry.bin";
    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback([&binary_data_storage](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
                             binary_data_storage.resize(p_res_size);
                             std::memcpy(binary_data_storage.data(), p_res, p_res_size);
                             return WBE::MEM_NULL;
                         })
                         .set_reserve_res_unit_storage_callback(
                             [&captured_ru_binary, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 EXPECT_EQ(p_size, sizeof(WBE::RUBinary));
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 captured_ru_binary = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUBinary>();
                                 return captured_ru_binary.force_cast<char>();
                             })
                         .build(ss);

    loader.populate_scheme(*res_chunk, binary_scheme);

    auto expected_binary = WBE::load_binary_file(WBE::get_resource_path(binary_scheme.path).string().c_str());
    ASSERT_NE(captured_ru_binary, nullptr);
    EXPECT_EQ(captured_ru_binary->size, expected_binary.size());
    EXPECT_EQ(binary_data_storage.size(), expected_binary.size());
    EXPECT_EQ(binary_data_storage, expected_binary);
}

TEST_F(ResourcePopulatorTest, ImageResourceGeneral) {
    WBE::ResourceUnitLoaderImage loader;
    std::stringstream ss;
    WBE::StackAllocator stack_allocator(1024);
    std::vector<char> image_data_storage;
    WBE::RefRes<WBE::RUImage> captured_ru_image = WBE::MEM_NULL;
    WBE::RUSchemaImage image_scheme;
    image_scheme.path = "assets/images/test_ppm3.ppm";
    image_scheme.color_space = WBE::ColorSpace::SRGB;
    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(
                             [&image_data_storage](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
                                 image_data_storage.resize(p_res_size);
                                 std::memcpy(image_data_storage.data(), p_res, p_res_size);
                                 return WBE::MEM_NULL;
                             }))
                         .set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)>(
                             [&captured_ru_image, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 EXPECT_EQ(p_size, sizeof(WBE::RUImage));
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 captured_ru_image = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUImage>();
                                 return captured_ru_image.force_cast<char>();
                             }))
                         .build(ss);
    loader.populate_scheme(*res_chunk, image_scheme);
    ASSERT_NE(captured_ru_image, nullptr);
    EXPECT_EQ(captured_ru_image->width, 16);
    EXPECT_EQ(captured_ru_image->height, 16);
    EXPECT_EQ(captured_ru_image->channel_count, 3);
    EXPECT_EQ(captured_ru_image->color_space, WBE::ColorSpace::SRGB);
    EXPECT_EQ(image_data_storage.size(), 16 * 16 * 3);
    // Check first pixel: should be red (255, 0, 0)
    EXPECT_EQ(static_cast<unsigned char>(image_data_storage.at(0)), 255);
    EXPECT_EQ(static_cast<unsigned char>(image_data_storage.at(1)), 0);
    EXPECT_EQ(static_cast<unsigned char>(image_data_storage.at(2)), 0);
    // Check second pixel: should be red (255, 0, 0)
    EXPECT_EQ(static_cast<unsigned char>(image_data_storage.at(3)), 255);
    EXPECT_EQ(static_cast<unsigned char>(image_data_storage.at(4)), 0);
    EXPECT_EQ(static_cast<unsigned char>(image_data_storage.at(5)), 0);
}

TEST_F(ResourcePopulatorTest, ImageResourceDefaultColorSpace) {
    WBE::ResourceUnitLoaderImage loader;
    std::stringstream ss;
    WBE::StackAllocator stack_allocator(1024);
    std::vector<char> image_data_storage;
    WBE::RefRes<WBE::RUImage> captured_ru_image = WBE::MEM_NULL;
    WBE::RUSchemaImage image_scheme;
    image_scheme.path = "assets/images/test_ppm3.ppm";
    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(
                             [&image_data_storage](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
                                 image_data_storage.resize(p_res_size);
                                 std::memcpy(image_data_storage.data(), p_res, p_res_size);
                                 return WBE::MEM_NULL;
                             }))
                         .set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)>(
                             [&captured_ru_image, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 EXPECT_EQ(p_size, sizeof(WBE::RUImage));
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 captured_ru_image = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUImage>();
                                 return captured_ru_image.force_cast<char>();
                             }))
                         .build(ss);
    loader.populate_scheme(*res_chunk, image_scheme);
    ASSERT_NE(captured_ru_image, nullptr);
    EXPECT_EQ(captured_ru_image->color_space, WBE::ColorSpace::SRGB);
}

TEST_F(ResourcePopulatorTest, TextureResourceGeneral) {
    WBE::ResourceUnitLoaderTexture loader;
    std::stringstream stream;
    WBE::StackAllocator stack_allocator(1024);
    WBE::RefRes<WBE::RUTexture> captured_texture = WBE::MEM_NULL;
    void* destructor_resource = nullptr;
    void (*destructor)(void*) = nullptr;
    WBE::RUSchemaTexture texture_scheme;
    texture_scheme.path = "assets/textures/default_albedo.ktx2";
    auto resource_chunk = MockResourceChunk::Builder()
                              .set_reserve_res_unit_storage_callback(
                                  [&captured_texture, &stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                      EXPECT_EQ(p_size, sizeof(WBE::RUTexture));
                                      WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                      captured_texture = WBE::RefRes<char>(mem_id, &stack_allocator).force_cast<WBE::RUTexture>();
                                      return captured_texture.force_cast<char>();
                                  })
                              .set_register_res_unit_destructor_callback(
                                  [&destructor_resource, &destructor](void* p_resource, void (*p_destructor)(void*)) {
                                      destructor_resource = p_resource;
                                      destructor = p_destructor;
                                  })
                              .build(stream);

    loader.populate_scheme(*resource_chunk, texture_scheme);

    ASSERT_NE(captured_texture, nullptr);
    EXPECT_NE(captured_texture->texture.get(), nullptr);
    ASSERT_NE(destructor_resource, nullptr);
    ASSERT_NE(destructor, nullptr);
    destructor(destructor_resource);
}

TEST_F(ResourcePopulatorTest, AudioResourceDecodesWavToFloatPcm) {
    constexpr uint32_t WAV_FRAME_COUNT = 11025;
    constexpr double WAV_SAMPLE_RATE = 44100.0;
    std::vector<char> data;
    WBE::RUAudio audio = load_audio(make_audio_scheme("assets/audio/test_sine_stereo.wav", false), data);

    EXPECT_EQ(audio.sample_rate, 44100U);
    EXPECT_EQ(audio.channels, 2U);
    EXPECT_EQ(audio.bit_depth, 32U);
    EXPECT_EQ(audio.frame_count, WAV_FRAME_COUNT);
    EXPECT_FLOAT_EQ(audio.duration, 0.25F);
    EXPECT_FALSE(audio.config.test(WBE::RUAudio::IS_STREAM));
    EXPECT_EQ(audio.data_size, size_t{WAV_FRAME_COUNT} * 2 * sizeof(float));
    ASSERT_EQ(data.size(), audio.data_size);
    // The left channel holds a 0.5-amplitude 440 Hz sine and the right channel is silent.
    for (size_t frame : {size_t{0}, size_t{25}, size_t{100}, size_t{WAV_FRAME_COUNT - 1}}) {
        const double expected = 0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(frame) / WAV_SAMPLE_RATE);
        EXPECT_NEAR(read_audio_sample(data, frame * 2), expected, 1e-3) << "frame " << frame;
        EXPECT_EQ(read_audio_sample(data, (frame * 2) + 1), 0.0F) << "frame " << frame;
    }
}

TEST_F(ResourcePopulatorTest, AudioResourceConvertsChannelsAndSampleRate) {
    WBE::RUSchemaAudio scheme = make_audio_scheme("assets/audio/test_sine_stereo.wav", false);
    scheme.channels = 1;
    scheme.sample_rate = 22050;
    std::vector<char> data;
    WBE::RUAudio audio = load_audio(scheme, data);

    EXPECT_EQ(audio.sample_rate, 22050U);
    EXPECT_EQ(audio.channels, 1U);
    EXPECT_NEAR(static_cast<double>(audio.frame_count), 11025.0 / 2.0, 1.0);
    EXPECT_NEAR(audio.duration, 0.25F, 1e-3F);
    ASSERT_EQ(data.size(), size_t{audio.frame_count} * sizeof(float));
    // Downmixing averages the 0.5-amplitude left channel with the silent right channel.
    float peak = 0.0F;
    for (size_t i = 0; i < audio.frame_count; ++i) {
        peak = std::max(peak, std::abs(read_audio_sample(data, i)));
    }
    EXPECT_NEAR(peak, 0.25F, 0.02F);
}

TEST_F(ResourcePopulatorTest, AudioResourceKeepsStreamEncoded) {
    std::vector<char> data;
    WBE::RUAudio audio = load_audio(make_audio_scheme("assets/audio/test_sine_mono.ogg", true), data);
    const auto encoded = WBE::load_binary_file<char>(WBE::get_resource_path("assets/audio/test_sine_mono.ogg").string().c_str());

    EXPECT_TRUE(audio.config.test(WBE::RUAudio::IS_STREAM));
    EXPECT_EQ(audio.sample_rate, 48000U);
    EXPECT_EQ(audio.channels, 1U);
    EXPECT_EQ(audio.bit_depth, 32U);
    // Vorbis decoders cannot report a length, so the loader counts the decoded frames.
    EXPECT_EQ(audio.frame_count, 12000U);
    EXPECT_FLOAT_EQ(audio.duration, 0.25F);
    EXPECT_EQ(audio.data_size, encoded.size());
    EXPECT_EQ(data, encoded);
}

TEST_F(ResourcePopulatorTest, AudioResourceDecodesVorbis) {
    std::vector<char> data;
    WBE::RUAudio audio = load_audio(make_audio_scheme("assets/audio/test_sine_mono.ogg", false), data);

    EXPECT_FALSE(audio.config.test(WBE::RUAudio::IS_STREAM));
    EXPECT_EQ(audio.channels, 1U);
    EXPECT_EQ(audio.frame_count, 12000U);
    ASSERT_EQ(data.size(), size_t{12000} * sizeof(float));
    float peak = 0.0F;
    for (size_t i = 0; i < audio.frame_count; ++i) {
        peak = std::max(peak, std::abs(read_audio_sample(data, i)));
    }
    EXPECT_GT(peak, 0.05F);
}

TEST_F(ResourcePopulatorTest, AudioResourceChecksManifestExpectations) {
    std::vector<char> data;
    WBE::RUSchemaAudio matching = make_audio_scheme("assets/audio/test_sine_stereo.wav", false);
    matching.bit_depth = 16;
    matching.frame_count = 11025;
    matching.duration = 0.25F;
    EXPECT_NO_THROW(load_audio(matching, data));

    WBE::RUSchemaAudio wrong_bit_depth = make_audio_scheme("assets/audio/test_sine_stereo.wav", false);
    wrong_bit_depth.bit_depth = 24;
    EXPECT_THROW(load_audio(wrong_bit_depth, data), std::runtime_error);

    WBE::RUSchemaAudio wrong_frame_count = make_audio_scheme("assets/audio/test_sine_stereo.wav", false);
    wrong_frame_count.frame_count = 11024;
    EXPECT_THROW(load_audio(wrong_frame_count, data), std::runtime_error);

    WBE::RUSchemaAudio wrong_duration = make_audio_scheme("assets/audio/test_sine_mono.ogg", true);
    wrong_duration.duration = 0.3F;
    EXPECT_THROW(load_audio(wrong_duration, data), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, AudioResourceRejectsInvalidInput) {
    std::vector<char> data;
    WBE::RUSchemaAudio zero_sample_rate = make_audio_scheme("assets/audio/test_sine_stereo.wav", false);
    zero_sample_rate.sample_rate = 0;
    EXPECT_THROW(load_audio(zero_sample_rate, data), std::runtime_error);

    WBE::RUSchemaAudio too_many_channels = make_audio_scheme("assets/audio/test_sine_stereo.wav", false);
    too_many_channels.channels = 1000;
    EXPECT_THROW(load_audio(too_many_channels, data), std::runtime_error);

    EXPECT_THROW(load_audio(make_audio_scheme("assets/audio/missing.wav", false), data), std::runtime_error);
    EXPECT_THROW(load_audio(make_audio_scheme("assets/images/test_ppm3.ppm", false), data), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, FontResourceGeneral) {
    // TODO: loader is not implemented yet.
}

TEST_F(ResourcePopulatorTest, MaterialResourceGeneral) {
    WBE::ResourceUnitLoaderMaterial loader;
    WBE::StackAllocator stack_allocator(64 * 1024);
    std::stringstream stream;
    WBE::RefRes<WBE::RUMaterial> captured_material;
    std::vector<std::string> created_ref_keys;

    auto push_to_stack = [&stack_allocator](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
        WBE::MemID mem_id = stack_allocator.allocate(p_res_size);
        std::memcpy(stack_allocator.get(mem_id), p_res, p_res_size);
        return WBE::RefRes<char>(mem_id, &stack_allocator);
    };

    std::array<std::byte, sizeof(WBE::RUTexture)> referenced_texture{};
    auto referenced_texture_ref = push_to_stack(referenced_texture.size(), referenced_texture.data()).force_cast<WBE::RUTexture>();
    std::unordered_map<std::string, WBE::RefRes<char>> reference_resources{
        {"normal_texture", referenced_texture_ref.force_cast<char>()},
    };

    auto res_chunk =
        MockResourceChunk::Builder()
            .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(push_to_stack))
            .set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)>(
                [&captured_material, &stack_allocator](std::string_view p_res_key, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = stack_allocator.allocate(p_size);
                    WBE::RefRes<char> saved_resource(mem_id, &stack_allocator);
                    if (p_res_key == "test_material") {
                        captured_material = saved_resource.force_cast<WBE::RUMaterial>();
                    }
                    return saved_resource;
                }))
            .set_create_ref_to_callback([&created_ref_keys, &reference_resources](
                                            std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                created_ref_keys.emplace_back(p_key);
                auto found_resource = reference_resources.find(created_ref_keys.back());
                ASSERT_NE(found_resource, reference_resources.end());
                p_assign_ref(found_resource->second);
            })
            .build(stream);

    std::string json_buffer = R"(
    {
        "id": "test_material",
        "type": "material",
        "textures": [
            {
                "texture_role": "normal",
                "texture_id": "normal_texture"
            },
            {
                "texture_role": "albedo",
                "texture_id": "normal_texture"
            }
        ]
    }
    )";
    WBE::ParserJSON parser;
    parser.parse_from_buffer(json_buffer);

    loader.populate_scheme_from_data(*res_chunk, parser.get_data());

    ASSERT_NE(captured_material, nullptr);
    ASSERT_NE(captured_material->resource_key, nullptr);
    EXPECT_STREQ(captured_material->resource_key, "test_material");
    ASSERT_EQ(captured_material->texture_count, 2U);
    ASSERT_NE(captured_material->textures, nullptr);
    EXPECT_STREQ(captured_material->textures.get()[0].role.name.get(), "normal");
    EXPECT_EQ(captured_material->textures.get()[0].role.hash, WBE::dynam_hash("normal"));
    EXPECT_EQ(captured_material->textures.get()[0].texture.get(), referenced_texture_ref.get());
    EXPECT_STREQ(captured_material->textures.get()[1].role.name.get(), "albedo");
    EXPECT_EQ(captured_material->textures.get()[1].role.hash, WBE::dynam_hash("albedo"));
    EXPECT_EQ(captured_material->textures.get()[1].texture.get(), referenced_texture_ref.get());
    ASSERT_EQ(created_ref_keys.size(), 2U);
    EXPECT_EQ(created_ref_keys[0], "normal_texture");
    EXPECT_EQ(created_ref_keys[1], "normal_texture");
}

TEST_F(ResourcePopulatorTest, GraphicsPipelineResourceGeneral) {
    WBE::ResourceUnitLoaderGraphicsPipeline loader;
    WBE::StackAllocator stack_allocator(64 * 1024);
    std::stringstream stream;
    WBE::RefRes<WBE::RUGraphicsPipeline> captured_pipeline;
    std::vector<std::string> created_ref_keys;

    auto push_to_stack = [&stack_allocator](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
        WBE::MemID mem_id = stack_allocator.allocate(p_res_size);
        std::memcpy(stack_allocator.get(mem_id), p_res, p_res_size);
        return WBE::RefRes<char>(mem_id, &stack_allocator);
    };

    WBE::RUShader vertex_shader;
    auto vertex_shader_ref = push_to_stack(sizeof(vertex_shader), &vertex_shader).force_cast<WBE::RUShader>();
    WBE::RUShader fragment_shader;
    auto fragment_shader_ref = push_to_stack(sizeof(fragment_shader), &fragment_shader).force_cast<WBE::RUShader>();
    std::unordered_map<std::string, WBE::RefRes<char>> reference_resources{
        {"vertex_shader", vertex_shader_ref.force_cast<char>()},
        {"fragment_shader", fragment_shader_ref.force_cast<char>()},
    };

    auto res_chunk =
        MockResourceChunk::Builder()
            .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(push_to_stack))
            .set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)>(
                [&captured_pipeline, &stack_allocator](std::string_view p_res_key, size_t p_size) -> WBE::RefRes<char> {
                    WBE::MemID mem_id = stack_allocator.allocate(p_size);
                    WBE::RefRes<char> saved_resource(mem_id, &stack_allocator);
                    if (p_res_key == "test_pipeline") {
                        captured_pipeline = saved_resource.force_cast<WBE::RUGraphicsPipeline>();
                    }
                    return saved_resource;
                }))
            .set_create_ref_to_callback([&created_ref_keys, &reference_resources](
                                            std::function<void(WBE::RefRes<char>)> p_assign_ref, std::string_view p_key) {
                created_ref_keys.emplace_back(p_key);
                auto found_resource = reference_resources.find(created_ref_keys.back());
                ASSERT_NE(found_resource, reference_resources.end());
                p_assign_ref(found_resource->second);
            })
            .build(stream);

    std::string json_buffer = R"(
    {
        "id": "test_pipeline",
        "type": "graphics_pipeline",
        "vertex_stage_id": "vertex_shader",
        "vertex_stage_entry": "vertex_main",
        "fragment_stage_id": "fragment_shader",
        "fragment_stage_entry": "fragment_main",
        "depth_test": false,
        "depth_write": false,
        "cull_backfaces": false,
        "conservative_rasterization": true
    }
    )";
    WBE::ParserJSON parser;
    parser.parse_from_buffer(json_buffer);

    loader.populate_scheme_from_data(*res_chunk, parser.get_data());

    ASSERT_NE(captured_pipeline, nullptr);
    ASSERT_NE(captured_pipeline->resource_key, nullptr);
    EXPECT_STREQ(captured_pipeline->resource_key, "test_pipeline");
    EXPECT_EQ(captured_pipeline->vertex_stage.get(), vertex_shader_ref.get());
    EXPECT_EQ(captured_pipeline->fragment_stage.get(), fragment_shader_ref.get());
    EXPECT_STREQ(captured_pipeline->vertex_stage_entry.get(), "vertex_main");
    EXPECT_STREQ(captured_pipeline->fragment_stage_entry.get(), "fragment_main");
    EXPECT_FALSE(captured_pipeline->depth_test);
    EXPECT_FALSE(captured_pipeline->depth_write);
    EXPECT_FALSE(captured_pipeline->cull_backfaces);
    EXPECT_TRUE(captured_pipeline->conservative_rasterization);
    ASSERT_EQ(created_ref_keys.size(), 2U);
    EXPECT_EQ(created_ref_keys[0], "vertex_shader");
    EXPECT_EQ(created_ref_keys[1], "fragment_shader");
}

TEST_F(ResourcePopulatorTest, MaterialTextureSlotValidation) {
    WBE::ResourceUnitLoaderMaterial loader;
    WBE::StackAllocator stack_allocator(64 * 1024);
    std::stringstream stream;

    auto push_to_stack = [&stack_allocator](size_t p_res_size, const void* p_res) -> WBE::RefRes<char> {
        WBE::MemID mem_id = stack_allocator.allocate(p_res_size);
        std::memcpy(stack_allocator.get(mem_id), p_res, p_res_size);
        return WBE::RefRes<char>(mem_id, &stack_allocator);
    };

    auto res_chunk = MockResourceChunk::Builder()
                         .set_push_data_callback(std::function<WBE::RefRes<char>(size_t, const void*)>(push_to_stack))
                         .set_reserve_res_unit_storage_callback(std::function<WBE::RefRes<char>(std::string_view, size_t)>(
                             [&stack_allocator](std::string_view, size_t p_size) -> WBE::RefRes<char> {
                                 WBE::MemID mem_id = stack_allocator.allocate(p_size);
                                 return WBE::RefRes<char>(mem_id, &stack_allocator);
                             }))
                         .build(stream);

    std::string json_buffer = R"(
    {
        "id": "test_material_missing_texture_binding",
        "type": "material",
        "textures": [{"texture_role": "albedo"}]
    }
    )";
    WBE::ParserJSON parser;
    parser.parse_from_buffer(json_buffer);

    EXPECT_THROW(loader.populate_scheme_from_data(*res_chunk, parser.get_data()), std::runtime_error);

    std::string arbitrary_texture_role_json = R"(
    {
        "id": "test_arbitrary_texture_role",
        "type": "material",
        "textures": [{"texture_role": "height", "texture_id": "height_texture"}]
    }
    )";
    WBE::ParserJSON arbitrary_texture_role_parser;
    arbitrary_texture_role_parser.parse_from_buffer(arbitrary_texture_role_json);
    EXPECT_NO_THROW(loader.populate_scheme_from_data(*res_chunk, arbitrary_texture_role_parser.get_data()));

    std::string empty_texture_role_json = R"(
    {
        "id": "test_empty_texture_role",
        "type": "material",
        "textures": [{"texture_role": "", "texture_id": "empty_texture"}]
    }
    )";
    WBE::ParserJSON empty_texture_role_parser;
    empty_texture_role_parser.parse_from_buffer(empty_texture_role_json);
    EXPECT_THROW(loader.populate_scheme_from_data(*res_chunk, empty_texture_role_parser.get_data()), std::runtime_error);

    std::string duplicate_texture_role_json = R"(
    {
        "id": "test_duplicate_texture_role",
        "type": "material",
        "textures": [
            {"texture_role": "albedo", "texture_id": "first_albedo"},
            {"texture_role": "albedo", "texture_id": "second_albedo"}
        ]
    }
    )";
    WBE::ParserJSON duplicate_texture_role_parser;
    duplicate_texture_role_parser.parse_from_buffer(duplicate_texture_role_json);
    EXPECT_THROW(loader.populate_scheme_from_data(*res_chunk, duplicate_texture_role_parser.get_data()), std::runtime_error);

    std::string colliding_texture_role_json = R"(
    {
        "id": "test_colliding_texture_role",
        "type": "material",
        "textures": [
            {"texture_role": "role_002876ae4c05a223", "texture_id": "first_texture"},
            {"texture_role": "role_2ffa43e2b7d24f56", "texture_id": "second_texture"}
        ]
    }
    )";
    static_assert(WBE::dynam_hash("role_002876ae4c05a223") == WBE::dynam_hash("role_2ffa43e2b7d24f56"));
    WBE::ParserJSON colliding_texture_role_parser;
    colliding_texture_role_parser.parse_from_buffer(colliding_texture_role_json);
    EXPECT_THROW(loader.populate_scheme_from_data(*res_chunk, colliding_texture_role_parser.get_data()), std::runtime_error);
}

TEST_F(ResourcePopulatorTest, VideoResourceGeneral) {
    // TODO: loader is not implemented yet.
}

#endif

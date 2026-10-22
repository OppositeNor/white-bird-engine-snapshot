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
#ifndef WBE_FILE_PARSER_JSON_TEST_HH
#define WBE_FILE_PARSER_JSON_TEST_HH

#include "core/parser/parser_json.hh"
#include "glm/detail/qualifier.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "parser_test_general.hh"
#include "utils/utils.hh"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace WBE = WhiteBirdEngine;

TEST(ParserJSONTest, ParseFromBuffer) {
    WBE::ParserJSON parser;
    test_parser_general(parser,
        R"({ "fruit" : "apple", "vegetable" : "lettice", "number" : 123 })",
        R"({ "vegetable" : "lettice", "numbers" : [123, -200, 5, 60, -123] })",
        R"({ "test_pair" : {
                                "test_key1" : "test_val1",
                                "test_key2" : 123,
                                "test_key3" : 3.14
                            }, "numbers" : [123, -200, 5, 60, -123] })");
}

// Test edge cases

TEST(ParserJSONTest, ParseFromBufferEdgeCases) {
    WBE::ParserJSON parser;
    // Empty JSON
    parser.parse_from_buffer(R"({})");
    ASSERT_TRUE(parser.get_data().get_all_keys().empty());

    // Nested empty objects
    parser.parse_from_buffer(R"({ "empty_obj": {} })");
    ASSERT_TRUE(parser.get_data().template get_value<WBE::DataValue>("empty_obj").get_all_keys().empty());

    // Deeply nested structures
    parser.parse_from_buffer(R"({ "level1": { "level2": { "level3": { "level4": "deep_value" } } } })");
    ASSERT_STREQ(parser.get_data()
                     .template get_value<WBE::DataValue>("level1")
                     .template get_value<WBE::DataValue>("level2")
                     .template get_value<WBE::DataValue>("level3")
                     .template get_value<std::string>("level4")
                     .c_str(),
        "deep_value");

    // Null values
    parser.parse_from_buffer(R"({ "key_with_null": null })");
    ASSERT_TRUE(parser.get_data().template get_value<std::nullptr_t>("key_with_null") == nullptr);
}

TEST(ParserJSONTest, ParseListOfObjects) {
    WBE::ParserJSON parser;

    // Test parsing an array of simple objects
    const std::string json_simple_objects = R"({
        "users": [
            {
                "id": 1,
                "name": "Alice",
                "active": true
            },
            {
                "id": 2,
                "name": "Bob",
                "active": false
            },
            {
                "id": 3,
                "name": "Charlie",
                "active": true
            }
        ]
    })";

    parser.parse_from_buffer(json_simple_objects);

    // Get the list of objects
    auto users = parser.get_value<std::vector<WBE::DataValue>>("users");
    ASSERT_EQ(users.size(), 3);

    // Verify first user
    ASSERT_EQ(users[0].get_value<int>("id"), 1);
    ASSERT_EQ(users[0].get_value<std::string>("name"), "Alice");
    ASSERT_EQ(users[0].get_value<bool>("active"), true);

    // Verify second user
    ASSERT_EQ(users[1].get_value<int>("id"), 2);
    ASSERT_EQ(users[1].get_value<std::string>("name"), "Bob");
    ASSERT_EQ(users[1].get_value<bool>("active"), false);

    // Verify third user
    ASSERT_EQ(users[2].get_value<int>("id"), 3);
    ASSERT_EQ(users[2].get_value<std::string>("name"), "Charlie");
    ASSERT_EQ(users[2].get_value<bool>("active"), true);
}

TEST(ParserJSONTest, ParseListOfNestedObjects) {
    WBE::ParserJSON parser;

    // Test parsing an array of objects with nested structures
    const std::string json_nested_objects = R"({
        "products": [
            {
                "id": 101,
                "name": "Laptop",
                "details": {
                    "brand": "TechCorp",
                    "model": "X1000",
                    "specs": {
                        "ram": "16GB",
                        "storage": "512GB SSD"
                    }
                },
                "tags": ["electronics", "computers", "portable"]
            },
            {
                "id": 102,
                "name": "Mouse",
                "details": {
                    "brand": "PeripheralCo",
                    "model": "M200",
                    "specs": {
                        "dpi": "1600",
                        "buttons": "5"
                    }
                },
                "tags": ["electronics", "accessories"]
            }
        ]
    })";

    parser.parse_from_buffer(json_nested_objects);

    // Get the list of products
    auto products = parser.get_value<std::vector<WBE::DataValue>>("products");
    ASSERT_EQ(products.size(), 2);

    // Verify first product
    ASSERT_EQ(products[0].get_value<int>("id"), 101);
    ASSERT_EQ(products[0].get_value<std::string>("name"), "Laptop");

    // Verify nested details of first product
    auto details1 = products[0].get_value<WBE::DataValue>("details");
    ASSERT_EQ(details1.get_value<std::string>("brand"), "TechCorp");
    ASSERT_EQ(details1.get_value<std::string>("model"), "X1000");

    // Verify deeply nested specs
    auto specs1 = details1.get_value<WBE::DataValue>("specs");
    ASSERT_EQ(specs1.get_value<std::string>("ram"), "16GB");
    ASSERT_EQ(specs1.get_value<std::string>("storage"), "512GB SSD");

    // Verify tags array within the object
    auto tags1 = products[0].get_value<std::vector<std::string>>("tags");
    ASSERT_EQ(tags1.size(), 3);
    ASSERT_EQ(tags1[0], "electronics");
    ASSERT_EQ(tags1[1], "computers");
    ASSERT_EQ(tags1[2], "portable");

    // Verify second product
    ASSERT_EQ(products[1].get_value<int>("id"), 102);
    ASSERT_EQ(products[1].get_value<std::string>("name"), "Mouse");

    auto details2 = products[1].get_value<WBE::DataValue>("details");
    auto specs2 = details2.get_value<WBE::DataValue>("specs");
    ASSERT_EQ(specs2.get_value<std::string>("dpi"), "1600");
    ASSERT_EQ(specs2.get_value<std::string>("buttons"), "5");

    auto tags2 = products[1].get_value<std::vector<std::string>>("tags");
    ASSERT_EQ(tags2.size(), 2);
    ASSERT_EQ(tags2[0], "electronics");
    ASSERT_EQ(tags2[1], "accessories");
}

TEST(ParserJSONTest, ParseEmptyAndMixedLists) {
    WBE::ParserJSON parser;

    // Test empty array
    const std::string json_empty_array = R"({
        "empty_list": [],
        "mixed_content": [
            { "type": "object", "value": 42 },
            { "type": "string", "value": "hello" },
            { "type": "boolean", "value": true }
        ]
    })";

    parser.parse_from_buffer(json_empty_array);

    // Verify empty list
    auto empty_list = parser.get_value<std::vector<WBE::DataValue>>("empty_list");
    ASSERT_EQ(empty_list.size(), 0);

    // Verify mixed content list
    auto mixed_content = parser.get_value<std::vector<WBE::DataValue>>("mixed_content");
    ASSERT_EQ(mixed_content.size(), 3);

    // Verify each mixed item
    ASSERT_EQ(mixed_content[0].get_value<std::string>("type"), "object");
    ASSERT_EQ(mixed_content[0].get_value<int>("value"), 42);

    ASSERT_EQ(mixed_content[1].get_value<std::string>("type"), "string");
    ASSERT_EQ(mixed_content[1].get_value<std::string>("value"), "hello");

    ASSERT_EQ(mixed_content[2].get_value<std::string>("type"), "boolean");
    ASSERT_EQ(mixed_content[2].get_value<bool>("value"), true);
}

TEST(ParserJSONTest, BufferRetrieval) {
    WBE::ParserJSON parser;

    const std::string json_data = R"({
        "short_text": "Hello",
        "medium_text": "This is a medium length text",
        "long_text": "This is a very long text that should test buffer capacity limits and bounds checking functionality",
        "empty_text": "",
        "special_chars": "Line1\nLine2\tTab\"Quote'"
    })";

    parser.parse_from_buffer(json_data);

    // Test retrieving into different buffer sizes
    WBE::Buffer<16> small_buffer{};
    WBE::Buffer<64> medium_buffer{};
    WBE::Buffer<128> large_buffer{};

    // Test short text fits in all buffers
    std::string key = "short_text";
    parser.get_data().get_value(key, small_buffer);
    ASSERT_STREQ(small_buffer.buffer, "Hello");

    parser.get_data().get_value(key, medium_buffer);
    ASSERT_STREQ(medium_buffer.buffer, "Hello");

    parser.get_data().get_value(key, large_buffer);
    ASSERT_STREQ(large_buffer.buffer, "Hello");

    // Test medium text fits in medium and large buffers
    key = "medium_text";
    parser.get_data().get_value(key, medium_buffer);
    ASSERT_STREQ(medium_buffer.buffer, "This is a medium length text");

    parser.get_data().get_value(key, large_buffer);
    ASSERT_STREQ(large_buffer.buffer, "This is a medium length text");

    // Test empty text
    key = "empty_text";
    parser.get_data().get_value(key, small_buffer);
    ASSERT_STREQ(small_buffer.buffer, "");

    // Test special characters
    key = "special_chars";
    parser.get_data().get_value(key, medium_buffer);
    ASSERT_STREQ(medium_buffer.buffer, "Line1\nLine2\tTab\"Quote'");
}

TEST(ParserJSONTest, BufferBoundsChecking) {
    WBE::ParserJSON parser;

    const std::string json_data = R"({
        "long_text": "This is a very long text that exceeds small buffer capacity"
    })";

    parser.parse_from_buffer(json_data);

    // Test that attempting to retrieve long text into small buffer throws exception
    WBE::Buffer<16> small_buffer{};
    std::string key = "long_text";
    ASSERT_THROW(parser.get_data().get_value(key, small_buffer), std::runtime_error);

    // Test that it works with appropriately sized buffer
    WBE::Buffer<128> large_buffer{};
    ASSERT_NO_THROW(parser.get_data().get_value(key, large_buffer));
    ASSERT_STREQ(large_buffer.buffer, "This is a very long text that exceeds small buffer capacity");
}

TEST(ParserJSONTest, BufferMaxCapacityRetrieval) {
    WBE::ParserJSON parser;

    // Test string that exactly fits buffer capacity (15 chars + null terminator for size 16)
    const std::string json_data = R"({
        "max_text": "123456789012345"
    })";

    parser.parse_from_buffer(json_data);

    WBE::Buffer<16> buffer{};
    std::string key = "max_text";
    parser.get_data().get_value(key, buffer);
    ASSERT_STREQ(buffer.buffer, "123456789012345");

    // Test string that's one character too long
    const std::string json_data_too_long = R"({
        "too_long": "1234567890123456"
    })";

    parser.parse_from_buffer(json_data_too_long);
    key = "too_long";
    ASSERT_THROW(parser.get_data().get_value(key, buffer), std::runtime_error);
}

TEST(ParserJSONTest, ComplexNestedStructures) {
    WBE::ParserJSON parser;

    // Test deeply nested structures with mixed types
    const std::string complex_json = R"({
        "application": {
            "name": "Test App",
            "version": "1.2.3",
            "features": {
                "authentication": {
                    "enabled": true,
                    "methods": ["oauth", "basic", "token"],
                    "config": {
                        "timeout": 3600,
                        "max_attempts": 3,
                        "providers": {
                            "google": {
                                "client_id": "google_client_123",
                                "enabled": true
                            },
                            "facebook": {
                                "app_id": "fb_app_456",
                                "enabled": false
                            }
                        }
                    }
                },
                "logging": {
                    "level": "info",
                    "destinations": ["console", "file", "remote"],
                    "rotation": {
                        "max_size": "10MB",
                        "max_files": 5,
                        "compress": true
                    }
                }
            },
            "dependencies": [
                {
                    "name": "nlohmann-json",
                    "version": "3.11.2",
                    "required": true
                },
                {
                    "name": "yaml-cpp",
                    "version": "0.7.0",
                    "required": true
                },
                {
                    "name": "optional-lib",
                    "version": "1.0.0",
                    "required": false
                }
            ]
        }
    })";

    parser.parse_from_buffer(complex_json);

    // Test top-level access
    auto app = parser.get_value<WBE::DataValue>("application");
    ASSERT_EQ(app.get_value<std::string>("name"), "Test App");
    ASSERT_EQ(app.get_value<std::string>("version"), "1.2.3");

    // Test nested features
    auto features = app.get_value<WBE::DataValue>("features");
    auto auth = features.get_value<WBE::DataValue>("authentication");
    ASSERT_EQ(auth.get_value<bool>("enabled"), true);

    // Test array within nested structure
    auto methods = auth.get_value<std::vector<std::string>>("methods");
    ASSERT_EQ(methods.size(), 3);
    ASSERT_EQ(methods[0], "oauth");
    ASSERT_EQ(methods[1], "basic");
    ASSERT_EQ(methods[2], "token");

    // Test deeply nested config
    auto config = auth.get_value<WBE::DataValue>("config");
    ASSERT_EQ(config.get_value<int>("timeout"), 3600);
    ASSERT_EQ(config.get_value<int>("max_attempts"), 3);

    // Test providers object
    auto providers = config.get_value<WBE::DataValue>("providers");
    auto google = providers.get_value<WBE::DataValue>("google");
    ASSERT_EQ(google.get_value<std::string>("client_id"), "google_client_123");
    ASSERT_EQ(google.get_value<bool>("enabled"), true);

    auto facebook = providers.get_value<WBE::DataValue>("facebook");
    ASSERT_EQ(facebook.get_value<std::string>("app_id"), "fb_app_456");
    ASSERT_EQ(facebook.get_value<bool>("enabled"), false);

    // Test other nested sections
    auto logging = features.get_value<WBE::DataValue>("logging");
    ASSERT_EQ(logging.get_value<std::string>("level"), "info");

    auto destinations = logging.get_value<std::vector<std::string>>("destinations");
    ASSERT_EQ(destinations.size(), 3);

    // Test dependencies array
    auto dependencies = app.get_value<std::vector<WBE::DataValue>>("dependencies");
    ASSERT_EQ(dependencies.size(), 3);
    ASSERT_EQ(dependencies[0].get_value<std::string>("name"), "nlohmann-json");
    ASSERT_EQ(dependencies[0].get_value<bool>("required"), true);
    ASSERT_EQ(dependencies[2].get_value<bool>("required"), false);
}

TEST(ParserJSONTest, ArrayVariations) {
    WBE::ParserJSON parser;

    // Test various array types and structures
    const std::string array_json = R"({
        "empty_array": [],
        "string_array": ["one", "two", "three"],
        "number_array": [1, 2, 3, 4, 5],
        "mixed_primitive_array": [1, "two", true, null, 3.14],
        "boolean_array": [true, false, true, true, false],
        "nested_arrays": [
            [1, 2, 3],
            ["a", "b", "c"],
            [true, false]
        ],
        "array_of_objects": [
            {"id": 1, "data": [10, 20]},
            {"id": 2, "data": [30, 40]},
            {"id": 3, "data": [50, 60]}
        ]
    })";

    parser.parse_from_buffer(array_json);

    // Test empty array
    auto empty_array = parser.get_value<std::vector<WBE::DataValue>>("empty_array");
    ASSERT_EQ(empty_array.size(), 0);

    // Test string array
    auto string_array = parser.get_value<std::vector<std::string>>("string_array");
    ASSERT_EQ(string_array.size(), 3);
    ASSERT_EQ(string_array[1], "two");

    // Test number array
    auto number_array = parser.get_value<std::vector<int>>("number_array");
    ASSERT_EQ(number_array.size(), 5);
    ASSERT_EQ(number_array[3], 4);

    // Test boolean array
    auto boolean_array = parser.get_value<std::vector<bool>>("boolean_array");
    ASSERT_EQ(boolean_array.size(), 5);
    ASSERT_EQ(boolean_array[0], true);
    ASSERT_EQ(boolean_array[1], false);

    // Test array of objects
    auto object_array = parser.get_value<std::vector<WBE::DataValue>>("array_of_objects");
    ASSERT_EQ(object_array.size(), 3);
    ASSERT_EQ(object_array[1].get_value<int>("id"), 2);

    auto data_array = object_array[1].get_value<std::vector<int>>("data");
    ASSERT_EQ(data_array.size(), 2);
    ASSERT_EQ(data_array[0], 30);
    ASSERT_EQ(data_array[1], 40);
}

TEST(ParserJSONTest, ErrorHandlingAndValidation) {
    WBE::ParserJSON parser;

    // Test malformed JSON handling
    ASSERT_THROW(parser.parse_from_buffer("{invalid json}"), std::exception);
    ASSERT_THROW(parser.parse_from_buffer("{\"unclosed\": \"string"), std::exception);
    ASSERT_THROW(parser.parse_from_buffer("{\"trailing\": \"comma\",}"), std::exception);

    // Test valid JSON with various edge cases
    parser.parse_from_buffer(R"({"null_value": null, "empty_string": "", "zero": 0})");

    // Test accessing non-existent keys
    ASSERT_FALSE(parser.contains("non_existent_key"));
    ASSERT_THROW(parser.get_value<std::string>("non_existent_key"), std::exception);

    // Test type mismatches and valid access
    ASSERT_TRUE(parser.contains("zero"));
    ASSERT_EQ(parser.get_value<int>("zero"), 0);

    // Test accessing valid keys
    ASSERT_TRUE(parser.contains("null_value"));
    ASSERT_TRUE(parser.contains("empty_string"));
    ASSERT_EQ(parser.get_value<std::string>("empty_string"), "");
}

TEST(ParserJSONTest, KeyManagementAndUtilities) {
    WBE::ParserJSON parser;

    const std::string test_json = R"({
        "key1": "value1",
        "key2": 123,
        "key3": true,
        "nested": {
            "subkey1": "subvalue1",
            "subkey2": 456
        },
        "array": [1, 2, 3]
    })";

    parser.parse_from_buffer(test_json);

    // Test get_all_keys functionality
    auto keys = parser.get_all_keys();
    ASSERT_EQ(keys.size(), 5);

    // Keys should include all top-level keys
    std::sort(keys.begin(), keys.end());
    std::vector<std::string> expected_keys = {"array", "key1", "key2", "key3", "nested"};
    std::sort(expected_keys.begin(), expected_keys.end());
    ASSERT_EQ(keys, expected_keys);

    // Test nested key access
    auto nested = parser.get_value<WBE::DataValue>("nested");
    auto nested_keys = nested.get_all_keys();
    ASSERT_EQ(nested_keys.size(), 2);

    std::sort(nested_keys.begin(), nested_keys.end());
    std::vector<std::string> expected_nested = {"subkey1", "subkey2"};
    std::sort(expected_nested.begin(), expected_nested.end());
    ASSERT_EQ(nested_keys, expected_nested);

    // Test contains functionality at different levels
    ASSERT_TRUE(parser.contains("key1"));
    ASSERT_TRUE(parser.contains("nested"));
    ASSERT_FALSE(parser.contains("subkey1")); // Not at top level

    ASSERT_TRUE(nested.contains("subkey1"));
    ASSERT_TRUE(nested.contains("subkey2"));
    ASSERT_FALSE(nested.contains("key1")); // Not in nested level
}

TEST(ParserJSONTest, ParseGLMVectors) {
    WBE::ParserJSON parser;

    const std::string glm_json = R"({
        "v2": { "x": 1.5, "y": 2.5 },
        "v3": { "x": 1.0, "y": 2.0, "z": 3.0 },
        "v4": { "x": -1.25, "y": 0.0, "z": 4.5, "w": 8.75 }
    })";

    parser.parse_from_buffer(glm_json);

    glm::vec2 v2 = parser.get_value<glm::vec2>("v2");
    ASSERT_FLOAT_EQ(v2.x, 1.5F);
    ASSERT_FLOAT_EQ(v2.y, 2.5F);

    glm::vec3 v3 = parser.get_value<glm::vec3>("v3");
    ASSERT_FLOAT_EQ(v3.x, 1.0F);
    ASSERT_FLOAT_EQ(v3.y, 2.0F);
    ASSERT_FLOAT_EQ(v3.z, 3.0F);

    glm::vec4 v4 = parser.get_value<glm::vec4>("v4");
    ASSERT_FLOAT_EQ(v4.x, -1.25F);
    ASSERT_FLOAT_EQ(v4.y, 0.0F);
    ASSERT_FLOAT_EQ(v4.z, 4.5F);
    ASSERT_FLOAT_EQ(v4.w, 8.75F);
}

TEST(ParserJSONTest, ParseGLMVectorsMissingFields) {
    WBE::ParserJSON parser;

    // v2 missing 'y', v3 missing 'z', v4 missing 'w'
    const std::string glm_json = R"({
        "v2": { "x": 1.0 },
        "v3": { "x": 1.0, "y": 2.0 },
        "v4": { "x": 0.0, "y": 0.0, "z": 0.0 }
    })";

    parser.parse_from_buffer(glm_json);

    // Accessing incomplete vectors should throw (nlohmann::json::out_of_range)
    ASSERT_THROW(parser.get_value<glm::vec2>("v2"), std::exception);
    ASSERT_THROW(parser.get_value<glm::vec3>("v3"), std::exception);
    ASSERT_THROW(parser.get_value<glm::vec4>("v4"), std::exception);
}

TEST(ParserJSONTest, GLMMat4RoundTripAndValidation) {
    WBE::ParserJSON parser;
    parser.parse_from_buffer(R"({"matrix": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]})");

    glm::mat4 matrix = parser.get_value<glm::mat4>("matrix");
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            ASSERT_FLOAT_EQ(matrix[column][row], static_cast<float>(column * 4 + row + 1));
        }
    }

    WBE::DataValue keyed_data;
    keyed_data.set_value("matrix", matrix);
    EXPECT_EQ(keyed_data.get_value<glm::mat4>("matrix"), matrix);

    WBE::DataValue root_data;
    root_data.set(matrix);
    EXPECT_EQ(root_data.get<glm::mat4>(), matrix);

    parser.parse_from_buffer(R"({"matrix": [1, 2, 3]})");
    EXPECT_THROW(parser.get_value<glm::mat4>("matrix"), std::runtime_error);
}

TEST(ParserJSONTest, JSONDataSetMethod) {
    // Test setting primitive values, arrays and nested DataValue via set()
    WBE::DataValue root;

    // Primitives
    root.set_value("fruit", std::string("apple"));
    root.set_value("number", 123);
    root.set_value("floating", 3.14);

    ASSERT_EQ(root.get_value<std::string>("fruit"), "apple");
    ASSERT_EQ(root.get_value<int>("number"), 123);
    ASSERT_DOUBLE_EQ(root.get_value<double>("floating"), 3.14);

    // Arrays (will be forwarded into underlying nlohmann::json)
    root.set_value("numbers", std::vector<int>{10, 20, 30});
    auto nums = root.get_value<std::vector<int>>("numbers");
    ASSERT_EQ(nums.size(), 3);
    ASSERT_EQ(nums[0], 10);
    ASSERT_EQ(nums[1], 20);
    ASSERT_EQ(nums[2], 30);

    // Nested DataValue by copy
    WBE::DataValue child;
    child.set_value("key1", "value1");
    child.set_value("key2", 777);
    root.set_value("child_copy", child); // should store child's internal json

    auto child_copy = root.get_value<WBE::DataValue>("child_copy");
    ASSERT_EQ(child_copy.get_value<std::string>("key1"), "value1");
    ASSERT_EQ(child_copy.get_value<int>("key2"), 777);

    // Nested DataValue by move (rvalue)
    WBE::DataValue child_move;
    child_move.set_value("moved", "yes");
    root.set_value("child_move", std::move(child_move));

    auto child_moved = root.get_value<WBE::DataValue>("child_move");
    ASSERT_EQ(child_moved.get_value<std::string>("moved"), "yes");
}

TEST(ParserJSONTest, JSONDataSetAssignMethod) {
    // Test the single-argument set(T&&) that assigns the current data value
    WBE::DataValue root;

    // Primitives
    root.set(std::string("apple"));
    ASSERT_EQ(root.get<std::string>(), "apple");

    root.set(123);
    ASSERT_EQ(root.get<int>(), 123);

    root.set(3.14);
    ASSERT_DOUBLE_EQ(root.get<double>(), 3.14);

    // Arrays
    root.set(std::vector<int>{10, 20, 30});
    auto nums = root.get<std::vector<int>>();
    ASSERT_EQ(nums.size(), 3);
    ASSERT_EQ(nums[0], 10);
    ASSERT_EQ(nums[1], 20);
    ASSERT_EQ(nums[2], 30);

    // Buffer
    WBE::Buffer<16> buf{};
    strcpy(buf.buffer, "bufval");
    root.set(buf);
    ASSERT_EQ(root.get<std::string>(), "bufval");

    // glm vector
    glm::vec3 v{1.0F, 2.0F, 3.0F};
    root.set(v);
    auto v_out = root.get<glm::vec3>();
    ASSERT_FLOAT_EQ(v_out.x, 1.0F);
    ASSERT_FLOAT_EQ(v_out.y, 2.0F);
    ASSERT_FLOAT_EQ(v_out.z, 3.0F);

    // Nested DataValue by copy
    WBE::DataValue child;
    child.set_value("key1", "value1");
    child.set_value("key2", 777);
    root.set(child);

    auto child_copy = root.get<WBE::DataValue>();
    ASSERT_EQ(child_copy.get_value<std::string>("key1"), "value1");
    ASSERT_EQ(child_copy.get_value<int>("key2"), 777);

    // Nested DataValue by move
    WBE::DataValue child_move2;
    child_move2.set_value("moved", "yes");
    root.set(std::move(child_move2));
    auto child_moved2 = root.get<WBE::DataValue>();
    ASSERT_EQ(child_moved2.get_value<std::string>("moved"), "yes");
}

#endif

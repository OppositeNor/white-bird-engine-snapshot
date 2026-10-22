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

#ifndef WBE_FILE_DESERIALIZER_TEST_HH
#define WBE_FILE_DESERIALIZER_TEST_HH
#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "utils/utils.hh"
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace WhiteBirdEngine {
struct WBE_META(WBE_SERIALIZABLE) TestEmptySerializable final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestEmptySerializable)
};

struct WBE_META(WBE_SERIALIZABLE) TestSerializable final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestSerializable)

    WBE_META(WBE_REFLECT)
    int32_t si32_test = 0;
    WBE_META(WBE_REFLECT)
    int64_t si64_test = 0;
    WBE_META(WBE_REFLECT)
    uint32_t ui32_test = 0;
    WBE_META(WBE_REFLECT)
    uint64_t ui64_test = 0;
    WBE_META(WBE_REFLECT)
    float f32_test = 0.0F;
    WBE_META(WBE_REFLECT)
    double f64_test = 0.0;
    WBE_META(WBE_REFLECT)
    glm::vec3 vec3_test = glm::vec3(0.0F);
    WBE_META(WBE_REFLECT)
    glm::vec4 vec4_test = glm::vec4(0.0F);
    WBE_META(WBE_REFLECT)
    std::string str_test;
    WBE_META(WBE_REFLECT)
    std::optional<std::string> optional_str_test;
    WBE_META(WBE_REFLECT)
    Buffer<16> buffer_test;
};

struct WBE_META(WBE_SERIALIZABLE) TestPartialStruct final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestPartialStruct)

    WBE_META(WBE_REFLECT)
    int32_t si32_test = 0;
    WBE_META(WBE_REFLECT)
    int64_t si64_test = 0;
    WBE_META(WBE_REFLECT)
    uint32_t ui32_test = 0;
    WBE_META(WBE_REFLECT)
    uint64_t ui64_test = 0;
};

struct WBE_META(WBE_SERIALIZABLE, WBE_SERIALIZATION_ALIASES(TestSerializableAlias, LegacySerializableAlias))
    TestSerializableAliases final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestSerializableAliases)

    WBE_META(WBE_REFLECT, WBE_SERIALIZATION_ALIASES(legacy_name, display_name))
    std::string canonical_name;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED, WBE_SERIALIZATION_ALIASES(required_alias))
    int32_t required_value = 0;
};

struct WBE_META(WBE_SERIALIZABLE) TestSerializableNesting final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestSerializableNesting)

    WBE_META(WBE_REFLECT)
    TestSerializable nested_test;
    WBE_META(WBE_REFLECT)
    int32_t nesting_id = 0;
    WBE_META(WBE_REFLECT)
    std::string name;
    WBE_META(WBE_REFLECT)
    Buffer<8> small_buffer;
};

// A simple container struct that holds vectors to test vector serialization
struct WBE_META(WBE_SERIALIZABLE) TestVectorContainer final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestVectorContainer)

    WBE_META(WBE_REFLECT)
    std::vector<int> ints;
    WBE_META(WBE_REFLECT)
    std::vector<std::string> strs;
    WBE_META(WBE_REFLECT)
    std::vector<TestSerializable> children;
};

// Depth-2 nested struct: contains a TestSerializable
struct WBE_META(WBE_SERIALIZABLE) TestDepth2 final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestDepth2)

    WBE_META(WBE_REFLECT)
    TestSerializable nested;
    WBE_META(WBE_REFLECT)
    int32_t depth2_id = 0;
    WBE_META(WBE_REFLECT)
    std::string depth2_name;
};

// Depth-3 nested struct: contains a TestDepth2
struct WBE_META(WBE_SERIALIZABLE) TestDepth3 final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestDepth3)
    WBE_META(WBE_REFLECT)
    TestDepth2 nested2;
    WBE_META(WBE_REFLECT)
    int32_t depth3_id = 0;
    WBE_META(WBE_REFLECT)
    std::string depth3_name;
};

// Container for vectors of deep structs
struct WBE_META(WBE_SERIALIZABLE) TestDeepVectorContainer final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestDeepVectorContainer)

    WBE_META(WBE_REFLECT)
    std::vector<TestDepth2> vec2;
    WBE_META(WBE_REFLECT)
    std::vector<TestDepth3> vec3;
};

// Inheritance test structures
struct WBE_META(WBE_SERIALIZABLE) TestInheritedBase : public Serializable {
    WBE_DECL_SERIALIZABLE(TestInheritedBase)

    WBE_META(WBE_REFLECT)
    int32_t base_id = 0;
    WBE_META(WBE_REFLECT)
    std::string base_name;
    WBE_META(WBE_REFLECT)
    float base_value = 0.0F;
};

struct WBE_META(WBE_SERIALIZABLE) TestInheritedChild : public TestInheritedBase {
    WBE_DECL_SERIALIZABLE(TestInheritedChild)

    WBE_META(WBE_REFLECT)
    int32_t child_id = 0;
    WBE_META(WBE_REFLECT)
    std::string child_name;
    WBE_META(WBE_REFLECT)
    double child_value = 0.0;
};

// Multi-level inheritance test
struct WBE_META(WBE_SERIALIZABLE) TestInheritedGrandchild : public TestInheritedChild {
    WBE_DECL_SERIALIZABLE(TestInheritedGrandchild)

    WBE_META(WBE_REFLECT)
    int32_t grandchild_id = 0;
    WBE_META(WBE_REFLECT)
    std::string grandchild_name;
    WBE_META(WBE_REFLECT)
    glm::vec3 grandchild_vector = glm::vec3(0.0F);
};

// Inheritance with nested serializable objects
struct WBE_META(WBE_SERIALIZABLE) TestInheritedWithNested : public TestInheritedBase {
    WBE_DECL_SERIALIZABLE(TestInheritedWithNested)

    WBE_META(WBE_REFLECT)
    TestSerializable nested_object;
    WBE_META(WBE_REFLECT)
    std::vector<int> child_numbers;
};

// Multiple inheritance test structures
struct WBE_META(WBE_SERIALIZABLE) TestMultipleInheritanceA : public Serializable {
    WBE_DECL_SERIALIZABLE(TestMultipleInheritanceA)

    WBE_META(WBE_REFLECT)
    int32_t a_id = 0;
    WBE_META(WBE_REFLECT)
    std::string a_name;
    WBE_META(WBE_REFLECT)
    float a_value = 0.0F;
};

struct WBE_META(WBE_SERIALIZABLE) TestMultipleInheritanceB : public Serializable {
    WBE_DECL_SERIALIZABLE(TestMultipleInheritanceB)
    WBE_META(WBE_REFLECT)
    int32_t b_id = 0;
    WBE_META(WBE_REFLECT)
    std::string b_name;
    WBE_META(WBE_REFLECT)
    double b_value = 0.0;
};

struct WBE_META(WBE_SERIALIZABLE) TestMultipleInheritanceChild final : public TestMultipleInheritanceA, public TestMultipleInheritanceB {
    WBE_DECL_SERIALIZABLE(TestMultipleInheritanceChild)

    WBE_META(WBE_REFLECT)
    int32_t child_id = 0;
    WBE_META(WBE_REFLECT)
    std::string child_name;
    WBE_META(WBE_REFLECT)
    glm::vec2 child_vector = glm::vec2(0.0F);
};

// Diamond inheritance pattern test
struct WBE_META(WBE_SERIALIZABLE) TestDiamondBase : public Serializable {
    WBE_DECL_SERIALIZABLE(TestDiamondBase)

    WBE_META(WBE_REFLECT)
    int32_t diamond_base_id = 0;
    WBE_META(WBE_REFLECT)
    std::string diamond_base_name;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondLeft : virtual public TestDiamondBase {
    WBE_DECL_SERIALIZABLE(TestDiamondLeft)

    WBE_META(WBE_REFLECT)
    int32_t left_id = 0;
    WBE_META(WBE_REFLECT)
    float left_value = 0.0F;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondRight : virtual public TestDiamondBase {
    WBE_DECL_SERIALIZABLE(TestDiamondRight)

    WBE_META(WBE_REFLECT)
    int32_t right_id = 0;
    WBE_META(WBE_REFLECT)
    double right_value = 0.0;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondChild final : public TestDiamondLeft, public TestDiamondRight {
    WBE_DECL_SERIALIZABLE(TestDiamondChild)

    WBE_META(WBE_REFLECT)
    int32_t diamond_child_id = 0;
    WBE_META(WBE_REFLECT)
    std::string diamond_child_name;
};

// Test structures with required fields and different inheritance patterns
struct WBE_META(WBE_SERIALIZABLE) TestRequiredFieldsBase : public Serializable {
    WBE_DECL_SERIALIZABLE(TestRequiredFieldsBase)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    int32_t required_id;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_name;
    WBE_META(WBE_REFLECT)
    float optional_value = 1.0F;
};

struct WBE_META(WBE_SERIALIZABLE) TestRequiredFieldsChild : public TestRequiredFieldsBase {
    WBE_DECL_SERIALIZABLE(TestRequiredFieldsChild)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_child_field;
    WBE_META(WBE_REFLECT)
    double optional_child_value = 2.0;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 required_vector;
};

struct WBE_META(WBE_SERIALIZABLE) TestMultipleRequiredInheritanceA : public Serializable {
    WBE_DECL_SERIALIZABLE(TestMultipleRequiredInheritanceA)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    int32_t required_a_id;
    WBE_META(WBE_REFLECT)
    std::string optional_a_name;
};

struct WBE_META(WBE_SERIALIZABLE) TestMultipleRequiredInheritanceB : public Serializable {
    WBE_DECL_SERIALIZABLE(TestMultipleRequiredInheritanceB)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    float required_b_value;
    WBE_META(WBE_REFLECT)
    std::string optional_b_desc;
};

struct WBE_META(WBE_SERIALIZABLE) TestMultipleRequiredChild final : public TestMultipleRequiredInheritanceA,
                                                                    public TestMultipleRequiredInheritanceB {
    WBE_DECL_SERIALIZABLE(TestMultipleRequiredChild)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_child_info;
    WBE_META(WBE_REFLECT)
    int32_t optional_child_count = 0;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondRequiredBase : public Serializable {
    WBE_DECL_SERIALIZABLE(TestDiamondRequiredBase)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_diamond_base;
    WBE_META(WBE_REFLECT)
    int32_t optional_diamond_id = 0;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondRequiredLeft : virtual public TestDiamondRequiredBase {
    WBE_DECL_SERIALIZABLE(TestDiamondRequiredLeft)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    double required_left_data;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondRequiredRight : virtual public TestDiamondRequiredBase {
    WBE_DECL_SERIALIZABLE(TestDiamondRequiredRight)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec2 required_right_vector;
};

struct WBE_META(WBE_SERIALIZABLE) TestDiamondRequiredChild final : public TestDiamondRequiredLeft, public TestDiamondRequiredRight {
    WBE_DECL_SERIALIZABLE(TestDiamondRequiredChild)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_final_field;
    WBE_META(WBE_REFLECT)
    bool optional_final_flag = false;
};

// Test structures with WBE_SERIALIZABLE_STATIC (static-only serialization)
struct WBE_META(WBE_SERIALIZABLE_STATIC) TestStaticSerializable {
    WBE_META(WBE_REFLECT)
    int32_t static_id = 0;
    WBE_META(WBE_REFLECT)
    std::string static_name;
    WBE_META(WBE_REFLECT)
    float static_value = 0.0F;
};

struct WBE_META(WBE_SERIALIZABLE_STATIC) TestStaticWithRequired {
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_static_field;
    WBE_META(WBE_REFLECT)
    int32_t optional_static_number = 42;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    glm::vec3 required_static_vector;
};

struct WBE_META(WBE_SERIALIZABLE_STATIC) TestStaticComplex {
    WBE_META(WBE_REFLECT)
    std::vector<int> static_numbers;
    WBE_META(WBE_REFLECT)
    std::vector<std::string> static_strings;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    Buffer<32> required_static_buffer;
    WBE_META(WBE_REFLECT)
    TestStaticSerializable nested_static;
};

struct WBE_META(WBE_SERIALIZABLE_STATIC) TestStaticEmpty {
    // Empty static serializable for testing
};

struct WBE_META(WBE_SERIALIZABLE_STATIC) TestStaticVectorTypes {
    WBE_META(WBE_REFLECT)
    glm::vec2 vec2_field = glm::vec2(0.0F);
    WBE_META(WBE_REFLECT)
    glm::vec3 vec3_field = glm::vec3(0.0F);
    WBE_META(WBE_REFLECT)
    glm::vec4 vec4_field = glm::vec4(0.0F);
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string required_identifier;
};

// Mixed container for testing various serialization scenarios
struct WBE_META(WBE_SERIALIZABLE) TestMixedContainer final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestMixedContainer)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<TestRequiredFieldsBase> required_objects;
    WBE_META(WBE_REFLECT)
    std::vector<TestStaticSerializable> static_objects;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    TestRequiredFieldsChild required_child;
    WBE_META(WBE_REFLECT)
    TestStaticComplex optional_static;
};

// Nested required fields test
struct WBE_META(WBE_SERIALIZABLE) TestNestedRequired final : public Serializable {
    WBE_DECL_SERIALIZABLE(TestNestedRequired)

    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    TestRequiredFieldsChild nested_required;
    WBE_META(WBE_REFLECT)
    int32_t optional_id = 0;
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::vector<TestStaticWithRequired> required_static_vector;
};

// Strict deserialization: unknown keys throw, including keys of a derived class when a strict base is deserialized alone.
struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION) TestStrictBase {
    WBE_META(WBE_REFLECT)
    int32_t base_value = 0;
    WBE_META(WBE_REFLECT, WBE_SERIALIZATION_ALIASES(base_alias))
    std::string base_name;
};

struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION) TestStrictChild : public TestStrictBase {
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    int32_t child_value = 0;
};

// A lenient class with a lenient base, holding a strict member: only the member's object is checked.
struct WBE_META(WBE_SERIALIZABLE_STATIC) TestLenientHolder {
    WBE_META(WBE_REFLECT)
    TestStrictBase strict_member;
    WBE_META(WBE_REFLECT)
    int32_t holder_value = 0;
};

// A strict class whose base is lenient: the base's fields are still known keys.
struct WBE_META(WBE_SERIALIZABLE_STATIC) TestLenientBase {
    WBE_META(WBE_REFLECT)
    int32_t lenient_base_value = 0;
};

struct WBE_META(WBE_SERIALIZABLE_STATIC, WBE_STRICT_DESERIALIZATION) TestStrictOverLenient : public TestLenientBase {
    WBE_META(WBE_REFLECT)
    int32_t strict_value = 0;
};

// Exercises method reflection; the metaparser collects only the methods annotated with WBE_REFLECT.
class WBE_META(WBE_TEST_METHODS) TestReflectedMethods final {
public:
    WBE_META(WBE_REFLECT)
    int32_t add(int32_t p_left, int32_t p_right) const {
        return p_left + p_right;
    }

    WBE_META(WBE_REFLECT, WBE_SERIALIZATION_ALIASES(make, build))
    static TestReflectedMethods create(const std::string& p_name, glm::vec3 p_offset) {
        TestReflectedMethods result;
        result.name = p_name;
        result.offset = p_offset;
        return result;
    }

    WBE_META(WBE_REFLECT)
    void reset() {
        name.clear();
    }

    WBE_META(WBE_REFLECT)
    void set(int32_t p_value) {
        value = p_value;
    }

    WBE_META(WBE_REFLECT)
    void set(const std::string& p_value) {
        name = p_value;
    }

    WBE_META(WBE_REFLECT)
    void ignore(float /*unnamed*/) {
    }

    WBE_META(WBE_REFLECT)
    const std::string& get_name() const {
        return name;
    }

    WBE_META(WBE_REFLECT)
    static const int32_t* find(const std::vector<int32_t>& p_values, int32_t p_target) {
        for (const int32_t& value : p_values) {
            if (value == p_target) {
                return &value;
            }
        }
        return nullptr;
    }

    int32_t not_annotated() const {
        return value;
    }

    WBE_META(WBE_TEST_ONLY)
    int32_t annotated_without_reflect() const {
        return value;
    }

private:
    std::string name;
    glm::vec3 offset{0.0F};
    int32_t value = 0;
};

/**
 * @brief Reflected enum with a signed narrow underlying type, aliases, and two enumerators sharing a value.
 */
enum class WBE_META(WBE_REFLECT_ENUM) TestEnumSigned : int8_t {
    NEGATIVE WBE_META(WBE_SERIALIZATION_ALIASES(neg, minus)) = -3,
    ZERO = 0,
    POSITIVE WBE_META(WBE_SERIALIZATION_ALIASES(pos)) = 100,
    // Shares the value of ZERO, which stays the canonical name.
    SAME_AS_ZERO = 0,
};

/**
 * @brief Reflected enum with a 64-bit unsigned underlying type and an ignored sentinel.
 */
enum class WBE_META(WBE_REFLECT_ENUM) TestEnumUnsigned64 : uint64_t {
    SMALL = 1,
    LARGE = 0xFFFF'FFFF'FFFF'FFF0ULL,
    IGNORED_SENTINEL WBE_META(WBE_REFLECT_ENUM_IGNORE) = 0xFFFF'FFFF'FFFF'FFF1ULL,
};

/**
 * @brief Not reflected; used to check ReflectedEnumConcept.
 */
enum class TestEnumUnreflected : uint8_t {
    VALUE,
};

/**
 * @brief Holds an unscoped reflected enum nested in a class.
 */
struct TestEnumHolder {
    /**
     * @brief Nested unscoped reflected enum.
     */
    enum WBE_META(WBE_REFLECT_ENUM) Nested : uint8_t {
        NESTED_A,
        NESTED_B WBE_META(WBE_SERIALIZATION_ALIASES(b)),
    };
};

/**
 * @brief Serializable with reflected enum fields.
 */
struct WBE_META(WBE_SERIALIZABLE_STATIC) TestEnumFields {
    WBE_META(WBE_REFLECT)
    TestEnumSigned signed_value = TestEnumSigned::ZERO;
    WBE_META(WBE_REFLECT)
    TestEnumUnsigned64 unsigned_value = TestEnumUnsigned64::SMALL;
    WBE_META(WBE_REFLECT)
    std::optional<TestEnumSigned> optional_value;
    WBE_META(WBE_REFLECT)
    std::vector<TestEnumHolder::Nested> nested_values;
};

} // namespace WhiteBirdEngine

#endif

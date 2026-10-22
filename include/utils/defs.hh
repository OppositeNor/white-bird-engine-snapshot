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
#ifndef WBE_FILE_DEFS_HH
#define WBE_FILE_DEFS_HH

#ifdef _DEBUG
#include <cassert>
// Block runs in debug builds.
#define WBE_DEBUG(...) {__VA_ARGS__}
// Assersion used in debug builds.
#define WBE_DEBUG_ASSERT(...) assert(__VA_ARGS__)
// Should enable profiling.
#define WBE_ENABLE_PROFILING 1
#else
// Block runs in debug builds.
#define WBE_DEBUG(...)
// Assersion used in debug builds.
#define WBE_DEBUG_ASSERT(...)
// Should enable profiling.
#define WBE_ENABLE_PROFILING 0
#endif

#if defined(__clang__)
// Disable optimization for a function.
#define WBE_NO_OPTIMIZE __attribute__((optnone))
#elif defined(__GNUC__)
// Disable optimization for a function.
#define WBE_NO_OPTIMIZE __attribute__((optimize("O0")))
#else
#pragma message("Please indicate the \"no optimization\" attribute here for your compiler.")
#define WBE_NO_OPTIMIZE
#endif

// Concatenate two tokens after macro-expanding them.
#define WBE_CONCAT(a, b) WBE_CONCAT_IMPL(a, b)
#define WBE_CONCAT_IMPL(a, b) a##b

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// Platform is little endian.
#define WBE_PLATFORM_LITTLE_ENDIAN

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
// Platform is big endian.
#define WBE_PLATFORM_BIG_ENDIAN

#endif

// Requires the struct to satisfy a specific concept.
#define WBE_REQUIRES_CONCEPT(requires_concept, StructType) static_assert(requires_concept<StructType>) // NOLINT

// Used inside a trait structure.
#define WBE_TRAIT(trait_name) using TraitType = trait_name

// Used inside a trait structure. Requires the trait to satisfy a specific
// concept.
#define WBE_TRAIT_REQUIRES(requires_concept) WBE_REQUIRES_CONCEPT(requires_concept, TraitType)

// Used for macro expansion.
#define WBE_EXPAND_STR(x) #x

// Define a type with metadata.
#define WBE_TYPE_METADATA(type_name)                                                                                        \
    template <>                                                                                                             \
    struct TypeIDTrait<type_name> {                                                                                         \
        static constexpr TypeID TYPE_ID = WhiteBirdEngine::static_hash(WBE_EXPAND_STR(type_name));                          \
        static constexpr std::string_view TYPE_NAME = WBE_EXPAND_STR(type_name);                                            \
    };                                                                                                                      \
    template <>                                                                                                             \
    struct TypeIDRevTrait<TypeIDTrait<type_name>::TYPE_ID> {                                                                \
        using Type = type_name;                                                                                             \
    };

// Get the type from an ID.
#define WBE_TYPE_FROM_ID(type_id) WhiteBirdEngine::TypeIDRevTrait<type_id>::Type
// Get the ID of a type.
#define WBE_TYPE_TO_ID(type_name) WhiteBirdEngine::TypeIDTrait<type_name>::TYPE_ID
// Get the name of the type ID.
#define WBE_TYPE_ID_NAME(type_id) WhiteBirdEngine::TypeIDTrait<WhiteBirdEngine::TypeIDRevTrait<type_id>::Type>::TYPE_NAME

// Kilobyte
#define WBE_K_B(x) ((x) * 1000)
// Megabyte
#define WBE_M_B(x) ((x) * WBE_K_B(1000))
// Gigabyte
#define WBE_G_B(x) ((x) * WBE_M_B(1000))
// Terabyte
#define WBE_T_B(x) ((x) * WBE_G_B(1000))
// Kibibyte
#define WBE_KI_B(x) ((x) * 1024)
// Mebibyte
#define WBE_MI_B(x) ((x) * WBE_KI_B(1024))
// Gibibyte
#define WBE_GI_B(x) ((x) * WBE_MI_B(1024))
// Tebibyte
#define WBE_TI_B(x) ((x) * WBE_GI_B(1024))

// NOLINTBEGIN
// Declare a CRTP concept for a CRTP base class.
#define WBE_DECL_CRTP_CONCEPT(Base)                                                                                         \
    template <typename T>                                                                                                   \
    concept Base##Concept = std::is_base_of_v<Base<std::remove_cvref_t<T>>, std::remove_cvref_t<T>>
// NOLINTEND

// Labels an component. Used for reflection.
#define WBE_COMPONENT(...)

#ifdef _DEBUG
#define WBE_VK_ENABLE_VALIDATION
#endif

// Default alignment
#define WBE_DEFAULT_ALIGNMENT 16 // TODO: set value according to the platform

#define WBE_FLOAT_TOLARENCE 0.0000001

// Avoiding two atomic variables be allocated into the same cacheline, causing
// memory sharing.
#define WBE_NO_FALSE_SHARING alignas(64)
#endif

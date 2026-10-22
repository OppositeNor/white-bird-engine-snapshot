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
#ifndef WBE_FILE_CORE_UTILS_HH
#define WBE_FILE_CORE_UTILS_HH

#include "core/allocator/heap_allocator_atomic_size_classified.hh"
#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_strong.hh"
#include "core/memory/reference_weak.hh"
#include "utils/defs.hh"
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

// Get a required optional value.
#define WBE_REQUIRED(optional_value) WhiteBirdEngine::required(#optional_value, (optional_value))

// Requires a pointer or reference to be non-null.
#define WBE_REQUIRES_VALID(optional_value) WhiteBirdEngine::requires_valid(#optional_value, (optional_value))

namespace WhiteBirdEngine {

/**
 * @class ThisRef
 * @brief Interface for objects that could be able to get a reference of
 * themselves (this_ref). If this object is not created as a reference, this_ref
 * will be MEM_NULL.
 *
 * @tparam T The type of the class.
 */
template <typename T>
class ThisRef {
public:
    ThisRef() = default;
    virtual ~ThisRef() = default;
    ThisRef(const ThisRef&) = default;
    ThisRef(ThisRef&&) = default;
    ThisRef& operator=(const ThisRef&) = default;
    ThisRef& operator=(ThisRef&&) = default;

    /**
     * @brief Set the reference referencing this instance.
     *
     * @param p_ref_of_this The reference referencing this instance.
     */
    void set_ref_of_this(Ref<T> p_ref_of_this) {
        WBE_DEBUG_ASSERT(this_ref.is_null());
        this_ref = p_ref_of_this;
    }

protected:
    /**
     * @brief Return the strong reference owning this instance.
     *
     * The result grants the same access as the Ref its owners already hold, so it is available from const members, for
     * example to share the instance with another owner.
     *
     * @return The reference set by make_ref().
     * @throws std::runtime_error If the instance was not created with make_ref() or is being destroyed.
     */
    Ref<T> acquire_this_ref() const {
        Ref<T> result = const_cast<RefWeak<T>&>(this_ref).lock();
        if (result == MEM_NULL) {
            throw std::runtime_error("Instance is not owned by a Ref.");
        }
        return result;
    }

    RefWeak<T> this_ref;
};

/**
 * @brief A better name for the required field.
 * We're using std::optional's valid check to check if a required field has passed in. However, "optional"
 * is definitely not a good word for fields that are "required". So we're making an alias here.
 *
 * @tparam T The type of the required value.
 */
template <typename T>
using Required = std::optional<T>; // What's optional is actually required...

template <typename T>
inline T& required(std::string_view p_value_name, std::optional<T>& p_optional_value) {
    if (!p_optional_value.has_value()) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value.value();
}

template <typename T>
inline const T& required(std::string_view p_value_name, const std::optional<T>& p_optional_value) {
    if (!p_optional_value.has_value()) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value.value();
}

template <typename T>
inline std::optional<T>& requires_valid(std::string_view p_value_name, std::optional<T>& p_optional_value) {
    if (!p_optional_value.has_value()) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value;
}

template <typename T>
inline std::optional<T> requires_valid(std::string_view p_value_name, std::optional<T>&& p_optional_value) {
    if (!p_optional_value.has_value()) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value;
}

template <typename T>
inline T* requires_valid(std::string_view p_value_name, T* p_optional_value) {
    if (p_optional_value == nullptr) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value;
}

template <typename T>
inline std::shared_ptr<T> requires_valid(std::string_view p_value_name, std::shared_ptr<T> p_optional_value) {
    if (p_optional_value == nullptr) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value;
}

template <typename T>
inline std::unique_ptr<T> requires_valid(std::string_view p_value_name, std::unique_ptr<T> p_optional_value) {
    if (p_optional_value == nullptr) {
        throw std::runtime_error(std::format("Option \"{}\" is required.", p_value_name));
    }
    return p_optional_value;
}

template <typename T>
T& required(std::string_view p_name, Ref<T> p_ref) {
    if (p_ref == MEM_NULL) {
        throw std::runtime_error(std::format("Reference: \"{}\" is required.", p_name));
    }
    return *p_ref;
}

template <typename T>
Ref<T> requires_valid(std::string_view p_name, Ref<T> p_ref) {
    if (p_ref == MEM_NULL) {
        throw std::runtime_error(std::format("Reference: \"{}\" is required to be valid.", p_name));
    }
    return p_ref;
}

template <typename T>
RefWeak<T> requires_valid(std::string_view p_name, RefWeak<T> p_ref) {
    if (!p_ref.is_valid() || p_ref == MEM_NULL) {
        throw std::runtime_error(std::format("Reference: \"{}\" is required to be valid.", p_name));
    }
    return p_ref;
}

using HeapAllocatorDefault = HeapAllocatorAtomicSizeClassified;

} // namespace WhiteBirdEngine

#endif

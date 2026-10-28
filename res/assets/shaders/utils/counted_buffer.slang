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
#ifndef WBE_FILE_COUNTED_BUFFER_SLANG
#define WBE_FILE_COUNTED_BUFFER_SLANG

#include "utils/utils.slang"

// A counted buffer starts with a header of WBE_COUNTED_BUFFER_HEADER_WORD_NUM uints, whatever its elements are, followed by
// its elements. Header word WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX holds the byte size of the elements, so views of different
// element types read the same buffer; the other header words are left to the algorithms using the buffer, such as the
// local sort count of a radix sort key buffer.
#define WBE_COUNTED_BUFFER_HEADER_WORD_NUM 4
#define WBE_COUNTED_BUFFER_HEADER_BYTE_SIZE (WBE_COUNTED_BUFFER_HEADER_WORD_NUM * 4)
#define WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX 0

/**
 * @brief A view of a storage buffer holding a variable number of elements after a header that records their byte size.
 * @details Element i starts at byte WBE_COUNTED_BUFFER_HEADER_BYTE_SIZE + i * sizeof(T). The capacity is the number of
 * elements the buffer has room for after the header, which the host sizes; size() never exceeds it except after a failed
 * try_atomic_expand(). get() and set() do not check their index against the size or the capacity. In shader-debug builds,
 * reading the size reports COUNTED_BUFFER_UNALIGNED_SIZE when the byte size is not a multiple of sizeof(T). The view is
 * copyable and does not own the buffer.
 * @tparam T Element type, made of 32-bit integer words, such as uint or uint2. Slang emits invalid SPIR-V for loads of
 * float types through a RWByteAddressBuffer held by a struct, so reinterpret floats with asuint() and asfloat().
 */
struct CountedBuffer<T> {
    /**
     * @brief The whole buffer, header included.
     */
    RWByteAddressBuffer buffer;
    /**
     * @brief Renderer frame slot receiving shader-debug errors.
     */
    uint frame_slot;

    /**
     * @brief View a buffer laid out as a counted buffer.
     * @param p_buffer The buffer.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    __init(RWByteAddressBuffer p_buffer, uint p_frame_slot) {
        buffer = p_buffer;
        frame_slot = p_frame_slot;
    }

    /**
     * @brief View the buffer at a descriptor-heap index, laid out as a counted buffer.
     * @param p_descriptor Descriptor-heap reference to the buffer, as declared with WBE_DESCRIPTOR; x is the heap index.
     * @param p_frame_slot Renderer frame slot receiving shader-debug errors.
     */
    __init(uint2 p_descriptor, uint p_frame_slot) {
        buffer = ResourceDescriptorHeap[p_descriptor.x];
        frame_slot = p_frame_slot;
    }

    /**
     * @brief Get the byte size of an element.
     * @return sizeof(T).
     */
    uint element_byte_size() {
        return uint(sizeof(T));
    }

    /**
     * @brief Get the byte size of the elements, as stored in the header.
     * @return The byte size, read without synchronization.
     */
    uint byte_size() {
        return buffer.Load(WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX * 4);
    }

    /**
     * @brief Overwrite the byte size of the elements.
     * @param p_byte_size The new byte size; must be a multiple of sizeof(T) and not exceed capacity() elements.
     */
    void set_byte_size(uint p_byte_size) {
        buffer.Store(WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX * 4, p_byte_size);
    }

    /**
     * @brief Get the element count.
     * @return byte_size() / sizeof(T), read without synchronization.
     */
    uint size() {
        return to_element_count(byte_size());
    }

    /**
     * @brief Overwrite the element count.
     * @param p_size The new element count; must not exceed capacity().
     */
    void set_size(uint p_size) {
        set_byte_size(p_size * element_byte_size());
    }

    /**
     * @brief Get the number of elements the buffer has room for after the header.
     * @return The capacity, queried from the buffer's dimensions.
     */
    uint capacity() {
        uint buffer_byte_size;
        buffer.GetDimensions(buffer_byte_size);
        return (buffer_byte_size - WBE_COUNTED_BUFFER_HEADER_BYTE_SIZE) / element_byte_size();
    }

    /**
     * @brief Read an element.
     * @param p_index The element index; must be less than capacity().
     * @return The element.
     */
    T get(uint p_index) {
        return buffer.Load<T>(get_byte_offset(p_index));
    }

    /**
     * @brief Write an element.
     * @param p_index The element index; must be less than capacity().
     * @param p_value The value written.
     */
    void set(uint p_index, T p_value) {
        buffer.Store<T>(get_byte_offset(p_index), p_value);
    }

    /**
     * @brief Get the byte offset of an element in buffer.
     * @param p_index The element index.
     * @return WBE_COUNTED_BUFFER_HEADER_BYTE_SIZE + p_index * sizeof(T).
     */
    uint get_byte_offset(uint p_index) {
        return WBE_COUNTED_BUFFER_HEADER_BYTE_SIZE + p_index * element_byte_size();
    }

    /**
     * @brief Read a header word.
     * @param p_word The header word index, in [0, WBE_COUNTED_BUFFER_HEADER_WORD_NUM).
     * @return The header word.
     */
    uint get_header(uint p_word) {
        return buffer.Load(p_word * 4);
    }

    /**
     * @brief Write a header word.
     * @details Writing WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX is equivalent to set_byte_size().
     * @param p_word The header word index, in [0, WBE_COUNTED_BUFFER_HEADER_WORD_NUM).
     * @param p_value The value written.
     */
    void set_header(uint p_word, uint p_value) {
        buffer.Store(p_word * 4, p_value);
    }

    /**
     * @brief Atomically add onto a header word.
     * @param p_word The header word index, in [0, WBE_COUNTED_BUFFER_HEADER_WORD_NUM).
     * @param p_value The value added.
     * @return The header word before the addition.
     */
    uint atomic_add_header(uint p_word, uint p_value) {
        uint original;
        buffer.InterlockedAdd(p_word * 4, p_value, original);
        return original;
    }

    /**
     * @brief Grow the element count, without synchronization.
     * @details Callers must ensure no other invocation accesses the byte size during the calling pass. In shader-debug
     * builds, growing past capacity() reports COUNTED_BUFFER_OVERFLOW; the count is not guarded otherwise.
     * @param p_size The number of elements added.
     * @return The previous element count, which is the index of the first added element.
     */
    uint expand(uint p_size) {
        uint first_index = size();
        check_fits(first_index, p_size);
        set_size(first_index + p_size);
        return first_index;
    }

    /**
     * @brief Atomically grow the element count, reserving a range of elements for the calling invocation.
     * @details In shader-debug builds, growing past capacity() reports COUNTED_BUFFER_OVERFLOW; the count is not guarded
     * otherwise. Use try_atomic_expand() when running out of room is allowed.
     * @param p_size The number of elements added.
     * @return The previous element count, which is the index of the first reserved element.
     */
    uint atomic_expand(uint p_size) {
        uint first_index =
            to_element_count(atomic_add_header(WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX, p_size * element_byte_size()));
        check_fits(first_index, p_size);
        return first_index;
    }

    /**
     * @brief Atomically grow the element count like atomic_expand(), failing when the reserved range does not fit.
     * @details On failure the element count is still grown, so it may exceed capacity(), and the range must not be
     * written. A later pass must clamp the element count to capacity() before the elements are consumed.
     * @param p_size The number of elements added.
     * @param p_first_index Receives the previous element count, which is the index of the first reserved element.
     * @return Whether the whole reserved range lies within capacity().
     */
    bool try_atomic_expand(uint p_size, out uint p_first_index) {
        p_first_index =
            to_element_count(atomic_add_header(WBE_COUNTED_BUFFER_BYTE_SIZE_INDEX, p_size * element_byte_size()));
        return fits(p_first_index, p_size);
    }

    uint to_element_count(uint p_byte_size) {
        WBE_SHADER_DEBUG_ASSERT(p_byte_size % element_byte_size() == 0,
            ShaderDebugError::COUNTED_BUFFER_UNALIGNED_SIZE, frame_slot);
        return p_byte_size / element_byte_size();
    }

    bool fits(uint p_first_index, uint p_size) {
        uint element_capacity = capacity();
        return p_size <= element_capacity && p_first_index <= element_capacity - p_size;
    }

    void check_fits(uint p_first_index, uint p_size) {
        WBE_SHADER_DEBUG_ASSERT(fits(p_first_index, p_size), ShaderDebugError::COUNTED_BUFFER_OVERFLOW, frame_slot);
    }
}

#endif

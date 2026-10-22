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
#ifndef WBE_FILE_RESOURCE_UNIT_HH
#define WBE_FILE_RESOURCE_UNIT_HH

#include "core/allocator/i_allocator.hh"
#include "core/memory/reference_raw.hh"
#include "core/reflection/reflection_defs.hh"
#include "core/reflection/serializable.hh"
#include "glm/ext/matrix_float4x4.hpp"
#include "resource/ktx_texture.hh"
#include "utils/utils.hh"
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
namespace WhiteBirdEngine {

/**
 * @class ResourceUnit
 * @brief Base type for manifest-loaded resource units.
 *
 * Resource units are stored inside resource chunks and may now be constructed
 * and destructed explicitly by the chunk. They are no longer required to be
 * trivially destructible.
 */
struct ResourceUnit {
    /**
     * @brief Stable key used to find this resource in its owning chunk.
     *
     * The string memory is owned by the resource chunk that stores this unit.
     * Anonymous resources leave this as nullptr.
     */
    const char* resource_key = nullptr;
};

template <typename T>
constexpr bool IS_RESOURCE_UNIT = std::is_base_of_v<ResourceUnit, std::remove_cvref_t<T>>;

template <typename T>
concept ResourceUnitConcept = IS_RESOURCE_UNIT<T>;

template <typename T>
using RefRes = RefRaw<T, IAllocator, false>;

/**
 * @class ResourceSchema
 * @brief The scheme of a resource unit.
 *
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchema : public Serializable {
    WBE_DECL_SERIALIZABLE(RUSchema)
    // The id of the resource. Default to empty string, which is an ananymous
    // resource.
    WBE_META(WBE_REFLECT)
    std::string id;
    // The type of the resource.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string type;
};

/**
 * @class RUSchemaAudio
 * @brief The scheme of RUAudio.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaAudio final : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaAudio)
    // Sample rate the audio is converted to when decoded. -1 keeps the file's
    // sample rate.
    WBE_META(WBE_REFLECT)
    int32_t sample_rate = -1;
    // The number of channels the audio is converted to when decoded (typically
    // 1 or 2). 0 keeps the file's channel count.
    WBE_META(WBE_REFLECT)
    uint32_t channels = 0;
    // The expected bit depth per sample of the file. -1 skips the check;
    // otherwise loading fails when the file differs.
    WBE_META(WBE_REFLECT)
    int32_t bit_depth = -1;
    // The expected duration of the file in seconds. -1 skips the check;
    // otherwise loading fails when the file differs by more than a millisecond.
    WBE_META(WBE_REFLECT)
    float duration = -1.0F;
    // The expected number of frames of the file at its own sample rate. -1
    // skips the check; otherwise loading fails when the file differs.
    WBE_META(WBE_REFLECT)
    int32_t frame_count = -1;
    // Is the audio loaded in stream.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    bool is_stream;
    // The path to the audio file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
};

/**
 * @class RUSchemaBinary
 * @brief The scheme of RUBinary.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaBinary : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaBinary)
    // The path to the binary file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
};

/**
 * @class RUSchemaFont
 * @brief The scheme of RUFont.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaFont : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaFont)
    // TODO
};

/**
 * @class RUSchemaImage
 * @brief The scheme of RUImage.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaImage : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaImage)
    // The channel count of the image. Read from file if set to 0.
    WBE_META(WBE_REFLECT)
    uint32_t channel_count = 0;
    // Image configurations.
    WBE_META(WBE_REFLECT)
    bool is_hdr = false;
    // Flip the image horizontally.
    WBE_META(WBE_REFLECT)
    bool flip_h = false;
    // Flip the image vertically.
    WBE_META(WBE_REFLECT)
    bool flip_v = false;
    // The color space of the image, deserialized from a ColorSpace name or alias such as "srgb".
    WBE_META(WBE_REFLECT)
    ColorSpace color_space = ColorSpace::SRGB;
    // The path to the image file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
};

/**
 * @class RUSchemaTexture
 * @brief The scheme of RUTexture.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaTexture : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaTexture)
    /** Path to a KTX 2 texture file. */
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
};

/**
 * @class RUSchemaText
 * @brief The scheme of RUText.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaText : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaText)
    // The path to the text file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
    // The encoding type, deserialized from an EncodeType name or alias such as "utf8". UTF8 by default.
    WBE_META(WBE_REFLECT)
    EncodeType encode_type = EncodeType::UTF8;
};

/**
 * @class RUSchemaScript
 * @brief The scheme of RUScript.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaScript : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaScript)
    // The path to the script source file.
    WBE_META(WBE_REFLECT, WBE_REFLECT_REQUIRED)
    std::string path;
};

/**
 * @class RUSchemaVideo
 * @brief The scheme of RUVideo.
 */
struct WBE_META(WBE_SERIALIZABLE) RUSchemaVideo : public RUSchema {
    WBE_DECL_SERIALIZABLE(RUSchemaVideo)
    // TODO
};

/**
 * @class RUAudio
 * @brief Audio data resource. Describes the decoded output format of an audio
 * file and references its chunk-owned data.
 *
 * Non-streamed audio stores the whole file decoded to interleaved 32-bit float
 * PCM. Streamed audio stores the encoded file bytes, which are decoded
 * incrementally during playback into the described output format, so playback
 * never reads files.
 *
 * When this unit references chunk-owned payload data, push that payload before
 * emplacing the resource unit so addresses stay stable inside the stack
 * allocator. Chunk teardown now runs any registered resource-unit destructors
 * before releasing the underlying stack storage.
 */
struct RUAudio : public ResourceUnit {
    /**
     * @brief Configurations to this resource unit.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum AudioConfig {
        // Should the audio be streamed. If true, then audio data holds the
        // encoded file bytes instead of decoded PCM.
        IS_STREAM,
        // To track the total number of bits.
        TOTAL_CONFIG_BIT,
    };
    // Sample rate of the decoded audio.
    uint32_t sample_rate;
    // The number of interleaved channels of the decoded audio (typically 1 or 2).
    uint32_t channels;
    // The bit depth per decoded sample. Decoded samples are always 32-bit float.
    uint32_t bit_depth;
    // The duration of the audio in seconds.
    float duration;
    // The number of decoded frames of the audio.
    uint32_t frame_count;
    // Configurations of the audio.
    std::bitset<TOTAL_CONFIG_BIT> config;
    // The data of the audio: decoded PCM, or the encoded file when IS_STREAM is set.
    RefRes<char> audio_data;
    // The size of audio_data in bytes.
    size_t data_size;
};

/**
 * @class RUBinary
 * @brief Binary resource unit. contains a size (in bytes) and a MemID pointing
 * to the data chunk that stores the data of the binary data.
 *
 * Push the payload data before emplacing this unit when it references
 * chunk-owned memory. Chunk teardown runs registered resource-unit destructors
 * before releasing the allocator storage that backs both the unit and its
 * payload.
 *
 */
struct RUBinary : public ResourceUnit {
    // The binary data.
    RefRes<char> data;
    // The size of the binary data.
    size_t size;
};

/**
 * @class RUFont
 * @brief Font resource unit.
 */
struct RUFont : public ResourceUnit {
    // TODO
};

/**
 * @class RUImage
 * @brief Image resource unit. Contains descriptions of an image
 * resource and a MemID to the resource of the image data.
 *
 * When this unit references chunk-owned image payload data, push that payload
 * before emplacing the unit so the stored references remain valid. Chunk
 * teardown runs registered resource-unit destructors before releasing the
 * underlying stack storage.
 *
 */
struct RUImage : public ResourceUnit {
    /**
     * @brief Configurations to this resource unit.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum ImageConfig {
        // Is the image an HDR image (32 bit instead of 8 bit)
        IS_HDR,
        // To track the total number of bits.
        TOTAL_CONFIG_BIT,
    };
    // The width of the image (in pixels).
    uint32_t width;
    // The height of the image (in pixels).
    uint32_t height;
    // The channel count of the image.
    uint32_t channel_count;
    // The color space of the image.
    ColorSpace color_space;
    // Image configurations.
    std::bitset<TOTAL_CONFIG_BIT> config;
    // The image data.
    RefRes<char> image_data;
};

/**
 * @class RUTexture
 * @brief Chunk-owned KTX 2 sampled-texture resource.
 *
 * The unit is move-only and releases its libktx object when its resource chunk
 * is unloaded.
 */
struct RUTexture : public ResourceUnit {
    RUTexture() = delete;
    explicit RUTexture(std::string_view p_path) : texture(p_path) {
    }
    ~RUTexture() = default;
    RUTexture(const RUTexture&) = delete;
    RUTexture(RUTexture&&) noexcept = default;
    RUTexture& operator=(const RUTexture&) = delete;
    RUTexture& operator=(RUTexture&&) noexcept = default;

    /** Loaded KTX 2 texture data. */
    KTXTexture2 texture;
};

/**
 * @class RUText
 * @brief Text resource unit. contains encoding type, size (in bytes, including
 * the NUL terminater) and a MemID pointing to the data chunk that stores the
 * data of the string (ends with a NUL terminater).
 *
 * Push the string payload before emplacing this unit when it references
 * chunk-owned memory. Chunk teardown runs registered resource-unit destructors
 * before releasing the allocator storage that backs both the unit and its
 * payload.
 */
struct RUText : public ResourceUnit {
    // The encoding type.
    EncodeType encode_type = EncodeType::UTF8;
    // The text data.
    RefRes<char> text_data;
    // The size of the text data, including the NUL terminator.
    size_t size;
};

/**
 * @class RUScript
 * @brief Script resource unit: the source of a script, for a script backend such as SBLua to load.
 *
 * The source is chunk-owned, UTF-8, and NUL-terminated. The unit is a shallow chunk view, copyable and movable.
 */
struct RUScript : public ResourceUnit {
    // The script source.
    RefRes<char> source;
    // The size of the source, including the NUL terminator.
    size_t size = 0;
};

/**
 * @class RUVideo
 * @brief Video resource unit.
 */
struct RUVideo : public ResourceUnit {
    // TODO
};

} // namespace WhiteBirdEngine

#endif

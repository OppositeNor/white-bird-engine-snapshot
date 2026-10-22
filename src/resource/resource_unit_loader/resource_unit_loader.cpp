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
#include "resource/resource_unit_loader/resource_unit_loader.hh"
#include "resource/resource_chunk/i_resource_chunk.hh"
#include "resource/resource_unit/resource_unit.hh"
#include "resource/resource_utils.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <miniaudio.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {
namespace {

RefRes<char> populate_text(IResourceChunk& p_res_chunk, std::string_view p_str) {
    std::string nul_terminated_text(p_str);
    nul_terminated_text.push_back('\0');
    return p_res_chunk.push_data(nul_terminated_text.size(), nul_terminated_text.data());
}

// Maximum difference between a manifest's expected audio duration and the file's duration.
constexpr double AUDIO_DURATION_TOLERANCE_SECONDS = 0.001;
// Frames decoded per read when a decoder cannot report its length.
constexpr ma_uint64 AUDIO_COUNT_CHUNK_FRAMES = 4096;

/**
 * Decoder over in-memory encoded audio that is uninitialized when it leaves scope. Neither copyable nor movable, because
 * miniaudio decoders hold their own address.
 */
class AudioMemoryDecoder {
public:
    AudioMemoryDecoder(std::span<const uint8_t> p_encoded, const ma_decoder_config& p_config, std::string_view p_path) {
        if (ma_decoder_init_memory(p_encoded.data(), p_encoded.size(), &p_config, &decoder) != MA_SUCCESS) {
            throw std::runtime_error(
                std::format("Failed to decode audio file: {}. Supported formats are WAV, FLAC, MP3, and Ogg Vorbis.", p_path));
        }
    }
    ~AudioMemoryDecoder() {
        ma_decoder_uninit(&decoder);
    }
    AudioMemoryDecoder(const AudioMemoryDecoder&) = delete;
    AudioMemoryDecoder(AudioMemoryDecoder&&) noexcept = delete;
    AudioMemoryDecoder& operator=(const AudioMemoryDecoder&) = delete;
    AudioMemoryDecoder& operator=(AudioMemoryDecoder&&) noexcept = delete;

    ma_decoder* get() {
        return &decoder;
    }

    // Count the decoder's frames, decoding the whole stream when its format cannot report a length (such as Vorbis).
    ma_uint64 count_frames(std::string_view p_path) {
        ma_uint64 length = 0;
        if (ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && length != 0) {
            return length;
        }
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0;
        if (ma_decoder_get_data_format(&decoder, &format, &channels, nullptr, nullptr, 0) != MA_SUCCESS) {
            throw std::runtime_error(std::format("Failed to read audio format: {}.", p_path));
        }
        std::vector<uint8_t> scratch(AUDIO_COUNT_CHUNK_FRAMES * ma_get_bytes_per_frame(format, channels));
        length = 0;
        while (true) {
            ma_uint64 frames_read = 0;
            ma_result result = ma_decoder_read_pcm_frames(&decoder, scratch.data(), AUDIO_COUNT_CHUNK_FRAMES, &frames_read);
            length += frames_read;
            if (result == MA_AT_END || frames_read == 0) {
                break;
            }
            if (result != MA_SUCCESS) {
                throw std::runtime_error(std::format("Failed to decode audio file: {}.", p_path));
            }
        }
        return length;
    }

private:
    ma_decoder decoder{};
};

// Fail when the manifest states a file property that differs from the file.
void validate_audio_expectations(const RUSchemaAudio& p_scheme, std::span<const uint8_t> p_encoded) {
    if (p_scheme.bit_depth == -1 && p_scheme.frame_count == -1 && p_scheme.duration == -1.0F) {
        return;
    }
    // Probing without output conversion reports the file's own format.
    const ma_decoder_config native_config = ma_decoder_config_init(ma_format_unknown, 0, 0);
    AudioMemoryDecoder native_decoder(p_encoded, native_config, p_scheme.path);
    ma_format native_format = ma_format_unknown;
    ma_uint32 native_sample_rate = 0;
    if (ma_decoder_get_data_format(native_decoder.get(), &native_format, nullptr, &native_sample_rate, nullptr, 0) != MA_SUCCESS) {
        throw std::runtime_error(std::format("Failed to read audio format: {}.", p_scheme.path));
    }
    if (p_scheme.bit_depth != -1) {
        const auto native_bit_depth = static_cast<int32_t>(ma_get_bytes_per_sample(native_format) * 8);
        if (native_bit_depth != p_scheme.bit_depth) {
            throw std::runtime_error(std::format(
                "Audio bit depth mismatch for {}: manifest expects {}, file has {}.", p_scheme.path, p_scheme.bit_depth, native_bit_depth));
        }
    }
    if (p_scheme.frame_count == -1 && p_scheme.duration == -1.0F) {
        return;
    }
    const ma_uint64 native_frame_count = native_decoder.count_frames(p_scheme.path);
    if (p_scheme.frame_count != -1 && std::cmp_not_equal(native_frame_count, p_scheme.frame_count)) {
        throw std::runtime_error(std::format(
            "Audio frame count mismatch for {}: manifest expects {}, file has {}.", p_scheme.path, p_scheme.frame_count, native_frame_count));
    }
    if (p_scheme.duration != -1.0F) {
        const double native_duration = static_cast<double>(native_frame_count) / static_cast<double>(native_sample_rate);
        if (std::abs(native_duration - static_cast<double>(p_scheme.duration)) > AUDIO_DURATION_TOLERANCE_SECONDS) {
            throw std::runtime_error(std::format(
                "Audio duration mismatch for {}: manifest expects {} s, file has {} s.", p_scheme.path, p_scheme.duration, native_duration));
        }
    }
}

void validate_audio_scheme(const RUSchemaAudio& p_scheme) {
    if (p_scheme.sample_rate != -1 && p_scheme.sample_rate <= 0) {
        throw std::runtime_error(std::format(
            "Invalid audio sample rate for {}: {}. Use -1 to keep the file's rate.", p_scheme.path, p_scheme.sample_rate));
    }
    if (p_scheme.channels > MA_MAX_CHANNELS) {
        throw std::runtime_error(std::format(
            "Invalid audio channel count for {}: {}. At most {} channels are supported.", p_scheme.path, p_scheme.channels, MA_MAX_CHANNELS));
    }
    if (p_scheme.bit_depth != -1 && p_scheme.bit_depth <= 0) {
        throw std::runtime_error(std::format("Invalid expected audio bit depth for {}: {}.", p_scheme.path, p_scheme.bit_depth));
    }
    if (p_scheme.frame_count < -1) {
        throw std::runtime_error(std::format("Invalid expected audio frame count for {}: {}.", p_scheme.path, p_scheme.frame_count));
    }
    if (p_scheme.duration != -1.0F && !(p_scheme.duration >= 0.0F)) {
        throw std::runtime_error(std::format("Invalid expected audio duration for {}: {}.", p_scheme.path, p_scheme.duration));
    }
}

} // namespace

std::any ResourceUnitLoaderAudio::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaAudio& p_scheme) const {
    validate_audio_scheme(p_scheme);
    auto file_path = get_resource_path(p_scheme.path);
    const std::vector<uint8_t> encoded = load_binary_file(file_path.string().c_str());
    if (encoded.empty()) {
        throw std::runtime_error(std::format("Failed to load audio: file is empty: {}.", file_path.string()));
    }
    validate_audio_expectations(p_scheme, encoded);

    // Decode to 32-bit float in the requested format; 0 keeps the file's channel count or sample rate.
    const auto output_sample_rate = p_scheme.sample_rate == -1 ? 0U : static_cast<ma_uint32>(p_scheme.sample_rate);
    ma_decoder_config output_config = ma_decoder_config_init(ma_format_f32, p_scheme.channels, output_sample_rate);
    RUAudio resource_unit{};
    ma_uint64 frame_count = 0;
    {
        AudioMemoryDecoder decoder(encoded, output_config, p_scheme.path);
        if (ma_decoder_get_data_format(decoder.get(), nullptr, &resource_unit.channels, &resource_unit.sample_rate, nullptr, 0) != MA_SUCCESS) {
            throw std::runtime_error(std::format("Failed to read audio format: {}.", p_scheme.path));
        }
        if (p_scheme.is_stream) {
            // Streams keep the encoded bytes, so only their decoded length is measured here.
            frame_count = decoder.count_frames(p_scheme.path);
        }
    }
    if (p_scheme.is_stream) {
        resource_unit.audio_data = p_res_chunk.push_data(encoded.size(), encoded.data());
        resource_unit.data_size = encoded.size();
        resource_unit.config.set(RUAudio::IS_STREAM);
    } else {
        void* pcm_frames = nullptr;
        if (ma_decode_memory(encoded.data(), encoded.size(), &output_config, &frame_count, &pcm_frames) != MA_SUCCESS) {
            throw std::runtime_error(std::format("Failed to decode audio file: {}.", p_scheme.path));
        }
        const size_t data_size = frame_count * ma_get_bytes_per_frame(ma_format_f32, resource_unit.channels);
        resource_unit.audio_data = p_res_chunk.push_data(data_size, pcm_frames);
        ma_free(pcm_frames, nullptr);
        resource_unit.data_size = data_size;
    }
    if (frame_count == 0) {
        throw std::runtime_error(std::format("Failed to load audio: file has no audio frames: {}.", p_scheme.path));
    }
    if (frame_count > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::format("Failed to load audio: {} has {} frames, more than the supported {}.",
            p_scheme.path,
            frame_count,
            std::numeric_limits<uint32_t>::max()));
    }
    resource_unit.frame_count = static_cast<uint32_t>(frame_count);
    resource_unit.bit_depth = static_cast<uint32_t>(ma_get_bytes_per_sample(ma_format_f32) * 8);
    resource_unit.duration = static_cast<float>(static_cast<double>(frame_count) / static_cast<double>(resource_unit.sample_rate));
    return p_res_chunk.push_res_unit(p_scheme.id, resource_unit).force_cast<RUAudio>();
}

std::any ResourceUnitLoaderBinary::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaBinary& p_scheme) const {
    auto file_path = get_resource_path(p_scheme.path);
    auto file_content = load_binary_file(file_path.string().c_str());
    auto data = p_res_chunk.push_data(file_content.size(), file_content.data());
    RUBinary resource_unit;
    resource_unit.data = data;
    resource_unit.size = file_content.size();
    return p_res_chunk.push_res_unit(p_scheme.id, resource_unit).force_cast<RUBinary>();
}

std::any ResourceUnitLoaderFont::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaFont& p_scheme) const {
    (void)p_res_chunk;
    (void)p_scheme;
    // TODO
    return std::any{};
}

std::any ResourceUnitLoaderImage::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaImage& p_scheme) const {
    auto file_path = get_resource_path(p_scheme.path);
    uint32_t width, height, file_channels;
    RUImage resource_unit;
    if (p_scheme.is_hdr) {
        auto image_data =
            load_image_data_hdr(file_path, p_scheme.channel_count, p_scheme.flip_h, p_scheme.flip_v, width, height, file_channels);
        resource_unit.width = width;
        resource_unit.height = height;
        resource_unit.channel_count = p_scheme.channel_count == 0 ? file_channels : p_scheme.channel_count;
        resource_unit.color_space = p_scheme.color_space;
        resource_unit.config.set(RUImage::IS_HDR);
        WBE_DEBUG_ASSERT(width * height * resource_unit.channel_count == image_data.size());
        auto data = p_res_chunk.push_data(image_data.size() * sizeof(float), image_data.data());
        resource_unit.image_data = data;
    } else {
        auto image_data =
            load_image_data(file_path, p_scheme.channel_count, p_scheme.flip_h, p_scheme.flip_v, width, height, file_channels);
        if (image_data.empty()) {
            throw std::runtime_error(std::format("Failed to load image data from file: {}", file_path.string()));
        }
        resource_unit.width = width;
        resource_unit.height = height;
        resource_unit.channel_count = p_scheme.channel_count == 0 ? file_channels : p_scheme.channel_count;
        resource_unit.color_space = p_scheme.color_space;
        auto data = p_res_chunk.push_data(image_data.size(), image_data.data());
        WBE_DEBUG_ASSERT(width * height * resource_unit.channel_count == image_data.size());
        resource_unit.image_data = data;
    }
    return p_res_chunk.push_res_unit(p_scheme.id, resource_unit).force_cast<RUImage>();
}

std::any ResourceUnitLoaderTexture::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaTexture& p_scheme) const {
    auto file_path = get_resource_path(p_scheme.path);
    return p_res_chunk.emplace_res_unit<RUTexture>(p_scheme.id, file_path.string());
}

std::any ResourceUnitLoaderText::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaText& p_scheme) const {
    auto file_path = get_resource_path(p_scheme.path);
    std::string file_content = load_text_file(file_path.string().c_str());
    auto data = populate_text(p_res_chunk, file_content);
    RUText resource_unit;
    resource_unit.text_data = data;
    resource_unit.size = file_content.size() + 1;
    resource_unit.encode_type = p_scheme.encode_type;
    return p_res_chunk.push_res_unit(p_scheme.id, resource_unit).force_cast<RUText>();
}

std::any ResourceUnitLoaderScript::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaScript& p_scheme) const {
    auto file_path = get_resource_path(p_scheme.path);
    std::string source = load_text_file(file_path.string().c_str());
    RUScript resource_unit;
    resource_unit.source = populate_text(p_res_chunk, source);
    resource_unit.size = source.size() + 1;
    return p_res_chunk.push_res_unit(p_scheme.id, resource_unit).force_cast<RUScript>();
}

std::any ResourceUnitLoaderVideo::populate_typed_scheme_ptr(IResourceChunk& p_res_chunk, const RUSchemaVideo& p_scheme) const {
    (void)p_res_chunk;
    (void)p_scheme;
    // TODO
    return std::any{};
}

} // namespace WhiteBirdEngine

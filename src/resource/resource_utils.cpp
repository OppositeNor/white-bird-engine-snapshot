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
#include "resource/resource_utils.hh"
#include "platform/file_system/file_system.hh"
#include "platform/file_system/path.hh"
#include "utils/defs.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stb_image.h>
#include <stb_image_write.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace WhiteBirdEngine {

static std::string remove_ppm_comments(std::string p_file_content) {
    while (p_file_content.contains('#')) {
        size_t pos = p_file_content.find('#');
        size_t end = p_file_content.find('\n', pos);
        p_file_content.erase(pos, end - pos);
    }
    return p_file_content;
}

static void write_ppm_pixel(uint8_t* p_data, size_t p_pixel_offset, uint32_t p_output_channels, uint8_t p_r, uint8_t p_g, uint8_t p_b) {
    uint8_t luminance = static_cast<uint8_t>(
        (static_cast<uint32_t>(p_r) * 77 + static_cast<uint32_t>(p_g) * 150 + static_cast<uint32_t>(p_b) * 29) >> 8);
    if (p_output_channels == 1 || p_output_channels == 2) {
        p_data[p_pixel_offset] = luminance;
        if (p_output_channels == 2) {
            p_data[p_pixel_offset + 1] = UINT8_MAX;
        }
        return;
    }
    p_data[p_pixel_offset + 0] = p_r;
    p_data[p_pixel_offset + 1] = p_g;
    p_data[p_pixel_offset + 2] = p_b;
    if (p_output_channels == 4) {
        p_data[p_pixel_offset + 3] = UINT8_MAX;
    }
}

static std::optional<std::vector<uint8_t>> load_ppm_data(
    const Path& p_path, uint32_t& p_width, uint32_t& p_height, uint32_t& p_file_channels, uint32_t p_desired_channels = 0) {
    // Load manually if PPM3
    if (p_path.extension() == ".ppm") {
        std::ifstream file(FileSystem::path_to_string(p_path), std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open PPM file: " + FileSystem::path_to_string(p_path));
        }
        std::string line;
        // Read header
        std::getline(file, line);
        if (line != "P3") {
            int width, height, file_channels;
            uint8_t* stbi_data = stbi_load(
                FileSystem::path_to_string(p_path).c_str(), &width, &height, &file_channels, static_cast<int>(p_desired_channels));
            if (stbi_data == nullptr) {
                throw std::runtime_error("Failed to load image file at path: " + FileSystem::path_to_string(p_path) +
                                         "\nReason: " + stbi_failure_reason());
            }
            uint32_t channels = p_desired_channels == 0 ? static_cast<uint32_t>(file_channels) : p_desired_channels;
            size_t total = static_cast<size_t>(width) * static_cast<size_t>(height) * channels;
            std::vector<uint8_t> result(stbi_data, stbi_data + total);
            stbi_image_free(stbi_data);
            p_width = width;
            p_height = height;
            p_file_channels = file_channels;
            return result;
        }
        uint32_t width, height;
        p_file_channels = 3;
        std::string str_content = load_text_file(p_path.string().c_str());
        std::stringstream content(remove_ppm_comments(str_content));
        // discard header line
        content >> line;
        content >> width >> height;
        double scaler;
        content >> scaler;
        scaler = 255.0 / scaler;
        uint32_t output_channels = p_desired_channels == 0 ? 3 : p_desired_channels;
        if (output_channels < 1 || output_channels > 4) {
            throw std::runtime_error("Failed to load PPM file: desired channel count is not supported.");
        }
        std::vector<uint8_t> result(static_cast<size_t>(width) * height * output_channels);
        // texture should go in reverse order
        for (uint32_t i = 0; i < width * height; ++i) {
            int r, g, b;
            content >> r >> g >> b;
            uint8_t scaled_r = static_cast<uint8_t>(static_cast<double>(r) * scaler);
            uint8_t scaled_g = static_cast<uint8_t>(static_cast<double>(g) * scaler);
            uint8_t scaled_b = static_cast<uint8_t>(static_cast<double>(b) * scaler);
            size_t pixel_offset = static_cast<size_t>(i) * output_channels;
            write_ppm_pixel(result.data(), pixel_offset, output_channels, scaled_r, scaled_g, scaled_b);
        }
        p_width = width;
        p_height = height;
        return result;
    }
    return std::nullopt; // Not PPM
}

std::vector<uint8_t> load_image_data(
    const Path& p_path, uint32_t p_desired_channels, bool p_flip_h, bool p_flip_v, uint32_t& p_width, uint32_t& p_height, uint32_t& p_file_channels) {
    auto ppm_result = load_ppm_data(p_path, p_width, p_height, p_file_channels, p_desired_channels);
    std::vector<uint8_t> result;
    if (ppm_result.has_value()) {
        result = std::move(*ppm_result);
    } else {
        int width, height, file_channels;
        uint8_t* stbi_data = stbi_load(
            FileSystem::path_to_string(p_path).c_str(), &width, &height, &file_channels, static_cast<int>(p_desired_channels));
        if (stbi_data == nullptr) {
            throw std::runtime_error("Failed to load image file at path: " + FileSystem::path_to_string(p_path) +
                                     "\nReason: " + stbi_failure_reason());
        }
        uint32_t channels = p_desired_channels == 0 ? static_cast<uint32_t>(file_channels) : p_desired_channels;
        size_t total = static_cast<size_t>(width) * static_cast<size_t>(height) * channels;
        result.assign(stbi_data, stbi_data + total);
        stbi_image_free(stbi_data);
        p_width = width;
        p_height = height;
        p_file_channels = file_channels;
    }
    if (p_flip_h) {
        if (p_desired_channels != 0) {
            img_flip_h(result.data(), p_width, p_height, p_desired_channels);
        } else {
            img_flip_h(result.data(), p_width, p_height, p_file_channels);
        }
    }
    if (p_flip_v) {
        if (p_desired_channels != 0) {
            img_flip_v(result.data(), p_width, p_height, p_desired_channels);
        } else {
            img_flip_v(result.data(), p_width, p_height, p_file_channels);
        }
    }
    WBE_DEBUG_ASSERT(p_width >= 0 && p_height >= 0 && p_file_channels >= 0);
    return result;
}

void write_image_data_png(const Path& p_path, std::span<const uint8_t> p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
    if (p_path.empty()) {
        throw std::runtime_error("Failed to write PNG image: destination path is empty.");
    }
    if (p_width == 0 || p_height == 0 || p_channels == 0 || p_channels > 4) {
        throw std::runtime_error("Failed to write PNG image: width, height, and channel count must be valid.");
    }
    if (p_width > static_cast<uint32_t>(std::numeric_limits<int>::max()) / p_channels ||
        p_height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Failed to write PNG image: dimensions exceed the PNG writer's limits.");
    }
    const size_t row_size = static_cast<size_t>(p_width) * p_channels;
    if (p_height > std::numeric_limits<size_t>::max() / row_size) {
        throw std::runtime_error("Failed to write PNG image: image byte size exceeds the CPU address space.");
    }
    const size_t expected_size = row_size * p_height;
    if (p_data.size_bytes() != expected_size) {
        throw std::runtime_error(std::format(
            "Failed to write PNG image: received {} bytes but {} bytes are required.", p_data.size_bytes(), expected_size));
    }
    if (stbi_write_png(FileSystem::path_to_string(p_path).c_str(),
            static_cast<int>(p_width),
            static_cast<int>(p_height),
            static_cast<int>(p_channels),
            p_data.data(),
            static_cast<int>(row_size)) == 0) {
        throw std::runtime_error(std::format("Failed to write PNG image: {}.", FileSystem::path_to_string(p_path)));
    }
}

std::vector<float> load_image_data_hdr(
    const Path& p_path, uint32_t p_desired_channels, bool p_flip_h, bool p_flip_v, uint32_t& p_width, uint32_t& p_height, uint32_t& p_file_channels) {

    int width, height, file_channels;
    float* stbi_data = stbi_loadf(
        FileSystem::path_to_string(p_path).c_str(), &width, &height, &file_channels, static_cast<int>(p_desired_channels));
    if (stbi_data == nullptr) {
        throw std::runtime_error("Failed to load HDR image file at path: " + FileSystem::path_to_string(p_path) +
                                 "\nReason: " + stbi_failure_reason());
    }
    WBE_DEBUG_ASSERT(width >= 0 && height >= 0 && file_channels >= 0);
    uint32_t channels = p_desired_channels == 0 ? static_cast<uint32_t>(file_channels) : p_desired_channels;
    size_t total = static_cast<size_t>(width) * static_cast<size_t>(height) * channels;
    std::vector<float> load_result(stbi_data, stbi_data + total);
    stbi_image_free(stbi_data);
    if (p_flip_h) {
        img_flip_h_hdr(load_result.data(), width, height, channels);
    }
    if (p_flip_v) {
        img_flip_v_hdr(load_result.data(), width, height, channels);
    }
    p_width = width;
    p_height = height;
    p_file_channels = file_channels;
    return load_result;
}

void img_flip_h(uint8_t* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
    uint32_t row_size = p_width * p_channels;
    for (uint32_t i = 0; i < p_height; ++i) {
        for (uint32_t j = 0; j < p_width / 2; ++j) {
            for (uint32_t k = 0; k < p_channels; ++k) {
                std::swap(p_data[i * row_size + j * p_channels + k], p_data[i * row_size + (p_width - j - 1) * p_channels + k]);
            }
        }
    }
}

void img_flip_v(uint8_t* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
    // Use temporary buffer to increase locality
    uint32_t row_size = p_width * p_channels;
    std::unique_ptr<uint8_t[]> temp = std::make_unique<uint8_t[]>(row_size);
    uint8_t* source_row = nullptr;
    uint8_t* target_row = nullptr;
    for (uint32_t i = 0; i < p_height / 2; ++i) {
        source_row = p_data + i * row_size;
        target_row = p_data + (p_height - i - 1) * row_size;
        memcpy(temp.get(), target_row, row_size);
        memcpy(target_row, source_row, row_size);
        memcpy(source_row, temp.get(), row_size);
    }
}

static uint8_t* get_pixel(uint8_t* p_data, uint32_t p_row, uint32_t p_col, uint32_t p_width, uint32_t p_channel) {
    return p_data + (p_row * p_width + p_col) * p_channel;
}

static uint8_t* get_pixel(uint8_t* p_data, uint32_t p_index, uint32_t p_channel) {
    return p_data + p_index * p_channel;
}

static void sq_img_transpose(uint8_t* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    for (uint32_t i = 0; i < p_height; ++i) {
        for (uint32_t j = i; j < p_width; ++j) {
            for (uint32_t k = 0; k < p_channel; ++k) {
                std::swap(get_pixel(p_data, i, j, p_width, p_channel)[k], get_pixel(p_data, j, i, p_width, p_channel)[k]);
            }
        }
    }
}

void img_rotate_cw(uint8_t* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    if (p_width != p_height) {
        throw std::runtime_error("Only allowed to rotate a square image.");
    }
    sq_img_transpose(p_data, p_width, p_height, p_channel);
    img_flip_h(p_data, p_width, p_height, p_channel);
}

void img_rotate_ccw(uint8_t* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    if (p_width != p_height) {
        throw std::runtime_error("Only allowed to rotate a square image.");
    }
    sq_img_transpose(p_data, p_width, p_height, p_channel);
    img_flip_v(p_data, p_width, p_height, p_channel);
}

void img_rotate_180(uint8_t* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    uint32_t image_size = p_width * p_height;
    uint32_t half_img_size = image_size >> 1;
    for (uint32_t i = 0; i < half_img_size; ++i) {
        for (uint32_t j = 0; j < p_channel; ++j) {
            std::swap(get_pixel(p_data, i, p_channel)[j], get_pixel(p_data, (image_size - i - 1), p_channel)[j]);
        }
    }
}

void img_flip_h_hdr(float* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
    uint32_t row_size = p_width * p_channels;
    for (uint32_t i = 0; i < p_height; ++i) {
        for (uint32_t j = 0; j < p_width / 2; ++j) {
            for (uint32_t k = 0; k < p_channels; ++k) {
                std::swap(p_data[i * row_size + j * p_channels + k], p_data[i * row_size + (p_width - j - 1) * p_channels + k]);
            }
        }
    }
}

void img_flip_v_hdr(float* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
    // Use temporary buffer to increase locality
    uint32_t row_size = p_width * p_channels;
    std::unique_ptr<float[]> temp = std::make_unique<float[]>(row_size);
    float* source_row;
    float* target_row;
    for (uint32_t i = 0; i < p_height / 2; ++i) {
        source_row = p_data + i * row_size;
        target_row = p_data + (p_height - i - 1) * row_size;
        memcpy(temp.get(), target_row, row_size * sizeof(float));
        memcpy(target_row, source_row, row_size * sizeof(float));
        memcpy(source_row, temp.get(), row_size * sizeof(float));
    }
}

void img_rotate_cw_hdr(float* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    if (p_width != p_height) {
        throw std::runtime_error("Only allowed to rotate a square image.");
    }
    // Transpose
    for (uint32_t i = 0; i < p_height; ++i) {
        for (uint32_t j = i; j < p_width; ++j) {
            for (uint32_t k = 0; k < p_channel; ++k) {
                std::swap(p_data[(i * p_width + j) * p_channel + k], p_data[(j * p_width + i) * p_channel + k]);
            }
        }
    }
    img_flip_h_hdr(p_data, p_width, p_height, p_channel);
}

void img_rotate_ccw_hdr(float* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    if (p_width != p_height) {
        throw std::runtime_error("Only allowed to rotate a square image.");
    }
    // Transpose
    for (uint32_t i = 0; i < p_height; ++i) {
        for (uint32_t j = i; j < p_width; ++j) {
            for (uint32_t k = 0; k < p_channel; ++k) {
                std::swap(p_data[(i * p_width + j) * p_channel + k], p_data[(j * p_width + i) * p_channel + k]);
            }
        }
    }
    img_flip_v_hdr(p_data, p_width, p_height, p_channel);
}

void img_rotate_180_hdr(float* p_data, uint32_t p_width, uint32_t p_height, uint32_t p_channel) {
    uint32_t image_size = p_width * p_height;
    uint32_t half_img_size = image_size >> 1;
    for (uint32_t i = 0; i < half_img_size; ++i) {
        for (uint32_t j = 0; j < p_channel; ++j) {
            std::swap(p_data[i * p_channel + j], p_data[(image_size - i - 1) * p_channel + j]);
        }
    }
}

} // namespace WhiteBirdEngine

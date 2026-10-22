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
#ifndef WBE_FILE_RESOURCE_UTILS_TEST_HH
#define WBE_FILE_RESOURCE_UTILS_TEST_HH

#include "generated/enum_reflection.gen.hh"
#include "global/global.hh"
#include "platform/file_system/path.hh"
#include "resource/resource_utils.hh"
#include "test_global.hh"
#include "utils/utils.hh"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace WBE = WhiteBirdEngine;

class WBEResourceUtilsTest : public ::testing::Test {
protected:
    virtual void SetUp() override {
        global = WBE::make_test_global();
    }

    virtual void TearDown() override {
        global.reset();
    }

    std::unique_ptr<WBE::Global> global;

    // Helper function to create test image data
    std::unique_ptr<uint8_t[]> create_test_image(uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
        auto data = std::make_unique<uint8_t[]>(p_width * p_height * p_channels);

        // Fill with test pattern: gradient with recognizable corner values
        for (uint32_t y = 0; y < p_height; ++y) {
            for (uint32_t x = 0; x < p_width; ++x) {
                uint32_t index = (y * p_width + x) * p_channels;

                // Create a pattern where we can easily verify transformations
                // Avoid divide by zero when width or height is 1
                for (uint32_t c = 0; c < p_channels; ++c) {
                    uint32_t x_component = (p_width > 1) ? (x * 255 / (p_width - 1)) : 255;
                    uint32_t y_component = (p_height > 1) ? (y * 255 / (p_height - 1)) : 255;
                    data[index + c] = static_cast<uint8_t>((x_component + y_component + c * 50) % 256);
                }
            }
        }

        return data;
    }

    // Helper function to create test HDR image data
    std::unique_ptr<float[]> create_test_image_hdr(uint32_t p_width, uint32_t p_height, uint32_t p_channels) {
        auto data = std::make_unique<float[]>(p_width * p_height * p_channels);

        // Fill with test pattern for HDR data
        for (uint32_t y = 0; y < p_height; ++y) {
            for (uint32_t x = 0; x < p_width; ++x) {
                uint32_t index = (y * p_width + x) * p_channels;

                for (uint32_t c = 0; c < p_channels; ++c) {
                    // Avoid divide by zero when width or height is 1
                    float x_component = (p_width > 1) ? static_cast<float>(x) / static_cast<float>(p_width - 1) : 1.0F;
                    float y_component = (p_height > 1) ? static_cast<float>(y) / static_cast<float>(p_height - 1) : 1.0F;
                    data[index + c] = x_component + y_component + static_cast<float>(c) * 0.1F;
                }
            }
        }

        return data;
    }

    // Helper to get pixel value
    uint8_t get_pixel(uint8_t* p_data, uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_channels, uint32_t p_channel = 0) {
        return p_data[(p_y * p_width + p_x) * p_channels + p_channel];
    }

    // Helper to get HDR pip_xel value
    float get_pixel_hdr(float* p_data, uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_channels, uint32_t p_channel = 0) {
        return p_data[(p_y * p_width + p_x) * p_channels + p_channel];
    }
};

// Test horizontal flip for regular images
TEST_F(WBEResourceUtilsTest, ImageFlipHorizontal) {
    const uint32_t width = 4;
    const uint32_t height = 3;
    const uint32_t channels = 3;

    auto original = create_test_image(width, height, channels);
    auto data = std::make_unique<uint8_t[]>(width * height * channels);
    std::memcpy(data.get(), original.get(), width * height * channels);

    // Store original corner values for verification
    auto top_left = get_pixel(original.get(), 0, 0, width, channels);
    auto top_right = get_pixel(original.get(), width - 1, 0, width, channels);
    auto bottom_left = get_pixel(original.get(), 0, height - 1, width, channels);
    auto bottom_right = get_pixel(original.get(), width - 1, height - 1, width, channels);

    WBE::img_flip_h(data.get(), width, height, channels);

    // After horizontal flip: left becomes right, right becomes left
    EXPECT_EQ(get_pixel(data.get(), width - 1, 0, width, channels), top_left);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, width, channels), top_right);
    EXPECT_EQ(get_pixel(data.get(), width - 1, height - 1, width, channels), bottom_left);
    EXPECT_EQ(get_pixel(data.get(), 0, height - 1, width, channels), bottom_right);
}

// Test vertical flip for regular images
TEST_F(WBEResourceUtilsTest, ImageFlipVertical) {
    const uint32_t width = 4;
    const uint32_t height = 3;
    const uint32_t channels = 3;

    auto original = create_test_image(width, height, channels);
    auto data = std::make_unique<uint8_t[]>(width * height * channels);
    std::memcpy(data.get(), original.get(), width * height * channels);

    // Store original corner values for verification
    auto top_left = get_pixel(original.get(), 0, 0, width, channels);
    auto top_right = get_pixel(original.get(), width - 1, 0, width, channels);
    auto bottom_left = get_pixel(original.get(), 0, height - 1, width, channels);
    auto bottom_right = get_pixel(original.get(), width - 1, height - 1, width, channels);

    WBE::img_flip_v(data.get(), width, height, channels);

    // After vertical flip: top becomes bottom, bottom becomes top
    EXPECT_EQ(get_pixel(data.get(), 0, height - 1, width, channels), top_left);
    EXPECT_EQ(get_pixel(data.get(), width - 1, height - 1, width, channels), top_right);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, width, channels), bottom_left);
    EXPECT_EQ(get_pixel(data.get(), width - 1, 0, width, channels), bottom_right);
}

// Test clockwise rotation for square images
TEST_F(WBEResourceUtilsTest, ImageRotateClockwise) {
    const uint32_t size = 4; // Square image only
    const uint32_t channels = 3;

    auto original = create_test_image(size, size, channels);
    auto data = std::make_unique<uint8_t[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels);

    // Store original corner values
    auto top_left = get_pixel(original.get(), 0, 0, size, channels);
    auto top_right = get_pixel(original.get(), size - 1, 0, size, channels);
    auto bottom_left = get_pixel(original.get(), 0, size - 1, size, channels);
    auto bottom_right = get_pixel(original.get(), size - 1, size - 1, size, channels);

    WBE::img_rotate_cw(data.get(), size, size, channels);

    // After 90° clockwise rotation:
    // top_left -> top_right, top_right -> bottom_right
    // bottom_left -> top_left, bottom_right -> bottom_left
    EXPECT_EQ(get_pixel(data.get(), size - 1, 0, size, channels), top_left);
    EXPECT_EQ(get_pixel(data.get(), size - 1, size - 1, size, channels), top_right);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), bottom_left);
    EXPECT_EQ(get_pixel(data.get(), 0, size - 1, size, channels), bottom_right);
}

// Test counter-clockwise rotation for square images
TEST_F(WBEResourceUtilsTest, ImageRotateCounterClockwise) {
    const uint32_t size = 4; // Square image only
    const uint32_t channels = 3;

    auto original = create_test_image(size, size, channels);
    auto data = std::make_unique<uint8_t[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels);

    // Store original corner values
    auto top_left = get_pixel(original.get(), 0, 0, size, channels);
    auto top_right = get_pixel(original.get(), size - 1, 0, size, channels);
    auto bottom_left = get_pixel(original.get(), 0, size - 1, size, channels);
    auto bottom_right = get_pixel(original.get(), size - 1, size - 1, size, channels);

    WBE::img_rotate_ccw(data.get(), size, size, channels);

    // After 90° counter-clockwise rotation:
    // top_left -> bottom_left, top_right -> top_left
    // bottom_left -> bottom_right, bottom_right -> top_right
    EXPECT_EQ(get_pixel(data.get(), 0, size - 1, size, channels), top_left);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), top_right);
    EXPECT_EQ(get_pixel(data.get(), size - 1, size - 1, size, channels), bottom_left);
    EXPECT_EQ(get_pixel(data.get(), size - 1, 0, size, channels), bottom_right);
}

// Test 180-degree rotation for square images
TEST_F(WBEResourceUtilsTest, ImageRotate180) {
    const uint32_t size = 4; // Square image only
    const uint32_t channels = 3;

    auto original = create_test_image(size, size, channels);
    auto data = std::make_unique<uint8_t[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels);

    // Store original corner values
    auto top_left = get_pixel(original.get(), 0, 0, size, channels);
    auto top_right = get_pixel(original.get(), size - 1, 0, size, channels);
    auto bottom_left = get_pixel(original.get(), 0, size - 1, size, channels);
    auto bottom_right = get_pixel(original.get(), size - 1, size - 1, size, channels);

    WBE::img_rotate_180(data.get(), size, size, channels);

    // After 180° rotation: corners swap diagonally
    EXPECT_EQ(get_pixel(data.get(), size - 1, size - 1, size, channels), top_left);
    EXPECT_EQ(get_pixel(data.get(), 0, size - 1, size, channels), top_right);
    EXPECT_EQ(get_pixel(data.get(), size - 1, 0, size, channels), bottom_left);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), bottom_right);
}

// Test horizontal flip for HDR images
TEST_F(WBEResourceUtilsTest, ImageFlipHorizontalHDR) {
    const uint32_t width = 4;
    const uint32_t height = 3;
    const uint32_t channels = 3;

    auto original = create_test_image_hdr(width, height, channels);
    auto data = std::make_unique<float[]>(width * height * channels);
    std::memcpy(data.get(), original.get(), width * height * channels * sizeof(float));

    // Store original corner values
    auto top_left = get_pixel_hdr(original.get(), 0, 0, width, channels);
    auto top_right = get_pixel_hdr(original.get(), width - 1, 0, width, channels);
    auto bottom_left = get_pixel_hdr(original.get(), 0, height - 1, width, channels);
    auto bottom_right = get_pixel_hdr(original.get(), width - 1, height - 1, width, channels);

    WBE::img_flip_h_hdr(data.get(), width, height, channels);

    // After horizontal flip: left becomes right, right becomes left
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), width - 1, 0, width, channels), top_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, 0, width, channels), top_right);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), width - 1, height - 1, width, channels), bottom_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, height - 1, width, channels), bottom_right);
}

// Test vertical flip for HDR images
TEST_F(WBEResourceUtilsTest, ImageFlipVerticalHDR) {
    const uint32_t width = 4;
    const uint32_t height = 3;
    const uint32_t channels = 3;

    auto original = create_test_image_hdr(width, height, channels);
    auto data = std::make_unique<float[]>(width * height * channels);
    std::memcpy(data.get(), original.get(), width * height * channels * sizeof(float));

    // Store original corner values
    auto top_left = get_pixel_hdr(original.get(), 0, 0, width, channels);
    auto top_right = get_pixel_hdr(original.get(), width - 1, 0, width, channels);
    auto bottom_left = get_pixel_hdr(original.get(), 0, height - 1, width, channels);
    auto bottom_right = get_pixel_hdr(original.get(), width - 1, height - 1, width, channels);

    WBE::img_flip_v_hdr(data.get(), width, height, channels);

    // After vertical flip: top becomes bottom, bottom becomes top
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, height - 1, width, channels), top_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), width - 1, height - 1, width, channels), top_right);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, 0, width, channels), bottom_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), width - 1, 0, width, channels), bottom_right);
}

// Test clockwise rotation for square HDR images
TEST_F(WBEResourceUtilsTest, ImageRotateClockwiseHDR) {
    const uint32_t size = 4; // Square image only
    const uint32_t channels = 3;

    auto original = create_test_image_hdr(size, size, channels);
    auto data = std::make_unique<float[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels * sizeof(float));

    // Store original corner values
    auto top_left = get_pixel_hdr(original.get(), 0, 0, size, channels);
    auto top_right = get_pixel_hdr(original.get(), size - 1, 0, size, channels);
    auto bottom_left = get_pixel_hdr(original.get(), 0, size - 1, size, channels);
    auto bottom_right = get_pixel_hdr(original.get(), size - 1, size - 1, size, channels);

    WBE::img_rotate_cw_hdr(data.get(), size, size, channels);

    // After 90° clockwise rotation
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), size - 1, 0, size, channels), top_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), size - 1, size - 1, size, channels), top_right);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, 0, size, channels), bottom_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, size - 1, size, channels), bottom_right);
}

// Test counter-clockwise rotation for square HDR images
TEST_F(WBEResourceUtilsTest, ImageRotateCounterClockwiseHDR) {
    const uint32_t size = 4; // Square image only
    const uint32_t channels = 3;

    auto original = create_test_image_hdr(size, size, channels);
    auto data = std::make_unique<float[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels * sizeof(float));

    // Store original corner values
    auto top_left = get_pixel_hdr(original.get(), 0, 0, size, channels);
    auto top_right = get_pixel_hdr(original.get(), size - 1, 0, size, channels);
    auto bottom_left = get_pixel_hdr(original.get(), 0, size - 1, size, channels);
    auto bottom_right = get_pixel_hdr(original.get(), size - 1, size - 1, size, channels);

    WBE::img_rotate_ccw_hdr(data.get(), size, size, channels);

    // After 90° counter-clockwise rotation
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, size - 1, size, channels), top_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, 0, size, channels), top_right);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), size - 1, size - 1, size, channels), bottom_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), size - 1, 0, size, channels), bottom_right);
}

// Test 180-degree rotation for square HDR images
TEST_F(WBEResourceUtilsTest, ImageRotate180HDR) {
    const uint32_t size = 4; // Square image only
    const uint32_t channels = 3;

    auto original = create_test_image_hdr(size, size, channels);
    auto data = std::make_unique<float[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels * sizeof(float));

    // Store original corner values
    auto top_left = get_pixel_hdr(original.get(), 0, 0, size, channels);
    auto top_right = get_pixel_hdr(original.get(), size - 1, 0, size, channels);
    auto bottom_left = get_pixel_hdr(original.get(), 0, size - 1, size, channels);
    auto bottom_right = get_pixel_hdr(original.get(), size - 1, size - 1, size, channels);

    WBE::img_rotate_180_hdr(data.get(), size, size, channels);

    // After 180° rotation: corners swap diagonally
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), size - 1, size - 1, size, channels), top_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, size - 1, size, channels), top_right);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), size - 1, 0, size, channels), bottom_left);
    EXPECT_FLOAT_EQ(get_pixel_hdr(data.get(), 0, 0, size, channels), bottom_right);
}

// Test color space string parsing
TEST_F(WBEResourceUtilsTest, ColorSpaceFromString) {
    // Test valid color spaces
    using ColorSpaceReflection = WBE::EnumReflection<WBE::ColorSpace>;
    EXPECT_EQ(ColorSpaceReflection::string_to_enum("srgb"), WBE::ColorSpace::SRGB);
    EXPECT_EQ(ColorSpaceReflection::string_to_enum("rgb"), WBE::ColorSpace::RGB);
    EXPECT_EQ(ColorSpaceReflection::string_to_enum("SRGB"), WBE::ColorSpace::SRGB);

    // Test invalid color space
    EXPECT_THROW((void)ColorSpaceReflection::string_to_enum("invalid"), std::runtime_error);
    EXPECT_THROW((void)ColorSpaceReflection::string_to_enum(""), std::runtime_error);
    EXPECT_THROW((void)ColorSpaceReflection::string_to_enum("Rgb"), std::runtime_error); // Case sensitive
}

// Test combined transformations
TEST_F(WBEResourceUtilsTest, CombinedTransformations) {
    const uint32_t width = 4;
    const uint32_t height = 4; // Square for rotation
    const uint32_t channels = 3;

    auto original = create_test_image(width, height, channels);
    auto data = std::make_unique<uint8_t[]>(width * height * channels);

    // Test: horizontal flip + vertical flip should equal 180° rotation
    std::memcpy(data.get(), original.get(), width * height * channels);
    WBE::img_flip_h(data.get(), width, height, channels);
    WBE::img_flip_v(data.get(), width, height, channels);

    auto rotation_data = std::make_unique<uint8_t[]>(width * height * channels);
    std::memcpy(rotation_data.get(), original.get(), width * height * channels);
    WBE::img_rotate_180(rotation_data.get(), width, height, channels);

    // Compare results
    for (uint32_t i = 0; i < width * height * channels; ++i) {
        EXPECT_EQ(data[i], rotation_data[i]) << "Mismatch at index " << i;
    }
}

// Test different channel counts
TEST_F(WBEResourceUtilsTest, DifferentChannelCounts) {
    const uint32_t size = 4;

    // Test with different channel counts: 1 (grayscale), 3 (RGB), 4 (RGBA)
    for (uint32_t channels : {1U, 3U, 4U}) {
        auto original = create_test_image(size, size, channels);
        auto data = std::make_unique<uint8_t[]>(size * size * channels);
        std::memcpy(data.get(), original.get(), size * size * channels);

        // Test that transformations don't crash with different channel counts
        WBE::img_flip_h(data.get(), size, size, channels);
        WBE::img_flip_v(data.get(), size, size, channels);
        WBE::img_rotate_cw(data.get(), size, size, channels);
        WBE::img_rotate_ccw(data.get(), size, size, channels);
        WBE::img_rotate_180(data.get(), size, size, channels);

        // Verify data is still valid (no crashes, no out-of-bounds access)
        SUCCEED();
    }
}

// Test edge cases with small images
TEST_F(WBEResourceUtilsTest, SmallImages) {
    // Test 1x1 square image
    const uint32_t size = 1;
    const uint32_t channels = 3;

    auto original = create_test_image(size, size, channels);
    auto data = std::make_unique<uint8_t[]>(size * size * channels);
    std::memcpy(data.get(), original.get(), size * size * channels);

    auto original_value = get_pixel(original.get(), 0, 0, size, channels);

    // All transformations should leave a 1x1 image unchanged
    WBE::img_flip_h(data.get(), size, size, channels);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), original_value);

    WBE::img_flip_v(data.get(), size, size, channels);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), original_value);

    WBE::img_rotate_cw(data.get(), size, size, channels);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), original_value);

    WBE::img_rotate_ccw(data.get(), size, size, channels);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), original_value);

    WBE::img_rotate_180(data.get(), size, size, channels);
    EXPECT_EQ(get_pixel(data.get(), 0, 0, size, channels), original_value);
}

TEST_F(WBEResourceUtilsTest, WriteImageDataPNG) {
    const WBE::Path output_path = std::filesystem::temp_directory_path() / "wbe_resource_utils_test.png";
    const std::vector<uint8_t> pixels{
        255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
    };

    ASSERT_NO_THROW(WBE::write_image_data_png(output_path, pixels, 2, 2, 4));
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    std::vector<uint8_t> loaded = WBE::load_image_data(output_path, 4, false, false, width, height, channels);
    EXPECT_EQ(width, 2U);
    EXPECT_EQ(height, 2U);
    EXPECT_EQ(channels, 4U);
    EXPECT_EQ(loaded, pixels);
    std::filesystem::remove(output_path);
}

TEST_F(WBEResourceUtilsTest, WriteImageDataPNGRejectsInvalidArguments) {
    const std::vector<uint8_t> pixels(4, UINT8_MAX);
    EXPECT_THROW(WBE::write_image_data_png(WBE::Path(), pixels, 1, 1, 4), std::runtime_error);
    EXPECT_THROW(WBE::write_image_data_png("unused.png", pixels, 0, 1, 4), std::runtime_error);
    EXPECT_THROW(WBE::write_image_data_png("unused.png", pixels, 1, 1, 5), std::runtime_error);
    EXPECT_THROW(WBE::write_image_data_png("unused.png", pixels, 2, 1, 4), std::runtime_error);
}

#endif

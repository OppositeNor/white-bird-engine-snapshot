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
/**
 * @file pixel_sort_utils.slang
 * @brief Key encoding shared by the pixel sort passes.
 * @details The passes sort the pixels of every line of the image independently. A line is a column, sorted from the top,
 * by default, and a row, sorted from the left, when WBE_PIXEL_SORT_HORIZONTAL is 1. A sorted pixel at position p of line
 * l, in an image of N lines of M pixels, gets one 32-bit key, (l * L + v) * M + p, where v is the quantized sort metric
 * of the pixel, which the host selects with PixelSortContrast.sort_metric, and L = floor((2^32 - 1) / (N * M)) is the
 * number of metric levels. Sorting the keys in ascending order groups them by line, orders each line by the metric, and
 * keeps the source position of every key. The contrast mask selects the pixels whose sort metric is inside a range, so it
 * follows the metric the pixels are sorted by. The keys of line l are
 * registered at the range of the key buffer that starts after the keys of every earlier line, so the sort keeps every
 * line in its range, and the k-th masked pixel of line l receives the k-th key of that range.
 *
 * With PixelSortContrast.sort_scope set to WBE_PIXEL_SORT_SCOPE_SPAN, every span, a maximal run of consecutive masked
 * pixels of a line, is sorted independently instead. The pixel at offset o of a span of length n whose first key is
 * registered at index s of the key data gets the key s * L + v * n + o. The keys of the span fill [s * L, (s + n) * L),
 * which ends where the range of the next span starts, so the sort keeps every span in its range with the same 32-bit
 * budget as the line scope.
 */
#ifndef WBE_FILE_PIXEL_SORT_UTILS_SLANG
#define WBE_FILE_PIXEL_SORT_UTILS_SLANG

#ifndef WBE_PIXEL_SORT_HORIZONTAL
/** 1 to sort the rows of the image from the left, 0 to sort its columns from the top. */
#define WBE_PIXEL_SORT_HORIZONTAL 0
#endif

/**
 * Value of the x channel of the pixel sort mask image for pixels outside the contrast mask. Masked pixels store their rank
 * among the masked pixels of their line in x, so the line length must be less than this value. With the span scope, they
 * also store their offset in their span in y and the length of their span in z.
 */
#define WBE_PIXEL_SORT_UNMASKED 0xFFFFu

/**
 * The first entries of the column offset buffer are metadata; the first one is the line count. The buffer holds one
 * entry per sorted line, which is a row in the horizontal variant.
 */
#define WBE_PIXEL_SORT_COLUMN_OFFSETS_START 4

/**
 * @brief Get the line coordinates of a pixel.
 * @param p_pixel The pixel coordinate; pass the image extent to get the line count and the line length.
 * @return The line of the pixel in x and its position along the line in y.
 */
uint2 pixel_sort_line_coordinates(uint2 p_pixel) {
#if WBE_PIXEL_SORT_HORIZONTAL
    return p_pixel.yx;
#else
    return p_pixel;
#endif
}

/**
 * @brief Get the pixel at line coordinates.
 * @param p_line_coordinates The line in x and the position along the line in y.
 * @return The pixel coordinate.
 */
uint2 pixel_sort_line_pixel(uint2 p_line_coordinates) {
#if WBE_PIXEL_SORT_HORIZONTAL
    return p_line_coordinates.yx;
#else
    return p_line_coordinates;
#endif
}

/**
 * @brief Get the key buffer index of a masked pixel.
 * @param p_column_offsets The column offset buffer from prefix_sum_pixel_sort_columns.
 * @param p_line The line of the pixel.
 * @param p_rank The rank of the pixel among the masked pixels of its line, from the mask image.
 * @return The index of the pixel's entry in the key data.
 */
uint pixel_sort_key_index(RWStructuredBuffer<uint> p_column_offsets, uint p_line, uint p_rank) {
    // The line starts where the previous line ends.
    uint line_start = p_line == 0 ? 0 : p_column_offsets[WBE_PIXEL_SORT_COLUMN_OFFSETS_START + p_line - 1];
    return line_start + p_rank;
}

/** PixelSortContrast.sort_metric value that sorts the pixels by luminance. */
#define WBE_PIXEL_SORT_METRIC_LUMINANCE 0u
/** PixelSortContrast.sort_metric value that sorts the pixels by hue. */
#define WBE_PIXEL_SORT_METRIC_HUE 1u
/** PixelSortContrast.sort_metric value that sorts the pixels by saturation. */
#define WBE_PIXEL_SORT_METRIC_SATURATION 2u

/** PixelSortContrast.sort_scope value that sorts all masked pixels of every line together. */
#define WBE_PIXEL_SORT_SCOPE_LINE 0u
/** PixelSortContrast.sort_scope value that sorts every span of consecutive masked pixels independently. */
#define WBE_PIXEL_SORT_SCOPE_SPAN 1u

/**
 * @struct PixelSortContrast
 * @brief The range of the sort metric of the pixels that are sorted, the metric they are sorted by, and the scope of each
 * sort, set by the host.
 */
struct PixelSortContrast {
    /** The smallest sort metric of a sorted pixel, in [0, 1]. */
    float min_metric;
    /** The largest sort metric of a sorted pixel, in [0, 1]. */
    float max_metric;
    /**
     * The metric the sorted pixels are ordered and masked by; the host must set one of the WBE_PIXEL_SORT_METRIC_ values.
     */
    uint sort_metric;
    /**
     * The pixels each sort covers; the host must set one of the WBE_PIXEL_SORT_SCOPE_ values. The span scope requires a
     * mask image with at least 3 channels.
     */
    uint sort_scope;
};

/**
 * @brief Get the luminance of a linear color.
 * @param p_color The linear color.
 * @return The luminance, clamped to [0, 1].
 */
float pixel_sort_luminance(float3 p_color) {
    return saturate(dot(p_color, float3(0.2126, 0.7152, 0.0722)));
}

/**
 * @brief Get the hue of a color.
 * @param p_color The color.
 * @return The hue in [0, 1), starting at red and increasing through yellow, green, cyan, blue, and magenta; 0 for gray.
 */
float pixel_sort_hue(float3 p_color) {
    float max_channel = max(p_color.r, max(p_color.g, p_color.b));
    float min_channel = min(p_color.r, min(p_color.g, p_color.b));
    float chroma = max_channel - min_channel;
    if (chroma <= 0.0) {
        return 0.0;
    }
    float hue;
    if (max_channel == p_color.r) {
        hue = (p_color.g - p_color.b) / chroma;
    } else if (max_channel == p_color.g) {
        hue = (p_color.b - p_color.r) / chroma + 2.0;
    } else {
        hue = (p_color.r - p_color.g) / chroma + 4.0;
    }
    // hue is in [-1, 5); shift it by a full turn and wrap the normalized value to [0, 1).
    return frac((hue + 6.0) / 6.0);
}

/**
 * @brief Get the saturation of a color.
 * @param p_color The color.
 * @return The HSV saturation, (max channel - min channel) / max channel, clamped to [0, 1]; 0 for black.
 */
float pixel_sort_saturation(float3 p_color) {
    float max_channel = max(p_color.r, max(p_color.g, p_color.b));
    float min_channel = min(p_color.r, min(p_color.g, p_color.b));
    if (max_channel <= 0.0) {
        return 0.0;
    }
    return saturate((max_channel - min_channel) / max_channel);
}

/**
 * @brief Get the sort metric of a color.
 * @param p_color The color.
 * @param p_sort_metric One of the WBE_PIXEL_SORT_METRIC_ values.
 * @return The luminance, hue, or saturation of p_color, in [0, 1].
 */
float pixel_sort_metric(float3 p_color, uint p_sort_metric) {
    switch (p_sort_metric) {
    case WBE_PIXEL_SORT_METRIC_LUMINANCE:
        return pixel_sort_luminance(p_color);
    case WBE_PIXEL_SORT_METRIC_HUE:
        return pixel_sort_hue(p_color);
    default:
        return pixel_sort_saturation(p_color);
    }
}

/**
 * @brief Check whether a pixel is inside the contrast mask.
 * @param p_color The linear color of the pixel.
 * @param p_contrast The contrast range and the sort metric.
 * @return Whether the sort metric of p_color is within [min_metric, max_metric].
 */
bool pixel_sort_in_contrast(float3 p_color, PixelSortContrast p_contrast) {
    float metric = pixel_sort_metric(p_color, p_contrast.sort_metric);
    return metric >= p_contrast.min_metric && metric <= p_contrast.max_metric;
}

/**
 * @brief Get the number of levels of the sort metric a key can hold for an image.
 * @param p_extent The image extent; the host must ensure the result is at least 2.
 * @return floor((2^32 - 1) / (p_extent.x * p_extent.y)).
 */
uint pixel_sort_key_levels(uint2 p_extent) {
    return 0xFFFFFFFFu / (p_extent.x * p_extent.y);
}

/**
 * @brief Quantize the sort metric of a color to the levels a key can hold.
 * @param p_color The color.
 * @param p_sort_metric One of the WBE_PIXEL_SORT_METRIC_ values.
 * @param p_metric_levels The number of levels, from pixel_sort_key_levels.
 * @return The quantized metric, in [0, p_metric_levels).
 */
uint pixel_sort_quantized_metric(float3 p_color, uint p_sort_metric, uint p_metric_levels) {
    float metric = pixel_sort_metric(p_color, p_sort_metric);
    return min(uint(metric * float(p_metric_levels)), p_metric_levels - 1);
}

/**
 * @brief Build the sort key of a pixel.
 * @param p_color The color of the pixel.
 * @param p_sort_metric The metric the key orders by; one of the WBE_PIXEL_SORT_METRIC_ values.
 * @param p_pixel The pixel coordinate.
 * @param p_extent The image extent.
 * @return The packed key.
 */
uint pixel_sort_make_key(float3 p_color, uint p_sort_metric, uint2 p_pixel, uint2 p_extent) {
    uint metric_levels = pixel_sort_key_levels(p_extent);
    uint quantized_metric = pixel_sort_quantized_metric(p_color, p_sort_metric, metric_levels);
    uint2 line_coordinates = pixel_sort_line_coordinates(p_pixel);
    uint line_length = pixel_sort_line_coordinates(p_extent).y;
    return (line_coordinates.x * metric_levels + quantized_metric) * line_length + line_coordinates.y;
}

/**
 * @brief Get the source pixel of a sort key.
 * @param p_key The key.
 * @param p_line The line the key belongs to.
 * @param p_extent The image extent.
 * @return The pixel the key was built from.
 */
uint2 pixel_sort_key_pixel(uint p_key, uint p_line, uint2 p_extent) {
    uint line_length = pixel_sort_line_coordinates(p_extent).y;
    return pixel_sort_line_pixel(uint2(p_line, p_key % line_length));
}

/**
 * @brief Build the span scope sort key of a pixel.
 * @param p_color The color of the pixel.
 * @param p_sort_metric The metric the key orders by; one of the WBE_PIXEL_SORT_METRIC_ values.
 * @param p_span_start The key data index of the first key of the pixel's span.
 * @param p_span_length The number of pixels in the span.
 * @param p_offset The offset of the pixel in the span.
 * @param p_extent The image extent.
 * @return The packed key, in [p_span_start * L, (p_span_start + p_span_length) * L).
 */
uint pixel_sort_make_span_key(
    float3 p_color, uint p_sort_metric, uint p_span_start, uint p_span_length, uint p_offset, uint2 p_extent) {
    uint metric_levels = pixel_sort_key_levels(p_extent);
    uint quantized_metric = pixel_sort_quantized_metric(p_color, p_sort_metric, metric_levels);
    return p_span_start * metric_levels + quantized_metric * p_span_length + p_offset;
}

/**
 * @brief Get the offset in its span of the source pixel of a span scope sort key.
 * @param p_key The key.
 * @param p_span_start The key data index of the first key of the span.
 * @param p_span_length The number of pixels in the span.
 * @param p_extent The image extent.
 * @return The offset of the pixel the key was built from.
 */
uint pixel_sort_span_key_offset(uint p_key, uint p_span_start, uint p_span_length, uint2 p_extent) {
    return (p_key - p_span_start * pixel_sort_key_levels(p_extent)) % p_span_length;
}

#endif

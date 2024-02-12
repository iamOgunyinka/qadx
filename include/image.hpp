/*
 * Copyright © 2024 Codethink Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace qad {
#pragma pack(push, 1)
struct BMPHeader {
  uint16_t type{};
  uint32_t size{};
  uint16_t reserved1{};
  uint16_t reserved2{};
  uint32_t offset{};
  uint32_t header_size{};
  int32_t width{};
  int32_t height{};
  uint16_t planes{};
  uint16_t bpp{};
  uint32_t compression{};
  uint32_t image_size{};
  int32_t x_resolution{};
  int32_t y_resolution{};
  uint32_t colors{};
  uint32_t important_colors{};
};
#pragma pack(pop)

using qad_screen_buffer_t = std::vector<unsigned char>;
struct image_data_t {
  enum class image_type_e {
    png,
    bmp,
    none,
  };

  qad_screen_buffer_t buffer;
  image_type_e type = image_type_e::none;
};

int encode_bmp(qad_screen_buffer_t const &data, int width, int height,
               int stride, image_data_t &screen_buffer);
void write_png(void *ptr, int width, int height, int pitch, int bpp, int rgb,
               image_data_t &screen_buffer);
} // namespace qad
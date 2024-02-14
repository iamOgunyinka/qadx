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

#include "enumerations.hpp"
#include "image.hpp"
#include <cstring>

namespace qadx {
// Write BMP file from image buffer
int encode_bmp(qad_screen_buffer_t const &raw_image_buffer, int const width,
               int const height, int const stride, image_data_t &image_data) {

  BMPHeader header{};
  header.type = 0x4D42;
  header.size = sizeof(BMPHeader) + stride * height;
  header.reserved1 = 0;
  header.reserved2 = 0;
  header.offset = sizeof(BMPHeader);
  header.header_size = sizeof(BMPHeader) - 14;
  header.width = width;
  header.height = height;
  header.planes = 1;
  header.bpp = 32; // 32 bits per pixel (RGBA)
  header.compression = 0;
  header.image_size = stride * height;
  header.x_resolution = 0;
  header.y_resolution = 0;
  header.colors = 0;
  header.important_colors = 0;

  auto const buffer_size = sizeof header + header.image_size;
  image_data.buffer.resize(buffer_size);
  image_data.type = image_type_e::bmp;

  unsigned char *out = image_data.buffer.data();
  memcpy(out, &header, sizeof header);
  out += sizeof header;

  // Write data to image
  memcpy(out, raw_image_buffer.data(), header.image_size);
  return 0;
}

} // namespace qadx
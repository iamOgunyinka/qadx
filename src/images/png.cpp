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

#include "image.hpp"
#include <cstring>
#include <png.h>
#include <stdexcept>

namespace qadx {
void write_png_func(png_structp png_ptr, png_bytep data, png_size_t length) {
  auto foo = (image_data_t *)png_get_io_ptr(png_ptr);
  auto &buffer = foo->buffer;
  size_t const buffer_size = buffer.size();
  size_t const new_size = buffer_size + length;

  buffer.resize(new_size);
  memcpy(buffer.data() + buffer_size, data, length);
}

void write_png(void *ptr, int const width, int const height, int const pitch,
               int const bpp, int const rgb, image_data_t &screen_buffer) {
  auto png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  auto png_info_ptr = png_create_info_struct(png_ptr);

  if (setjmp(png_jmpbuf(png_ptr)))
    throw std::runtime_error("unable to do setjmp");

  png_set_compression_level(png_ptr, 1);
  png_set_write_fn(png_ptr, &screen_buffer, write_png_func, nullptr);

  int const colour_type = PNG_COLOR_TYPE_RGB;
  png_set_IHDR(png_ptr, png_info_ptr, width, height, 8, colour_type,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png_ptr, png_info_ptr);

  // TODO: Big assumption
  if (bpp == 32) {
    if (!rgb)
      png_set_bgr(png_ptr);
    png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
  }

  for (int j = 0; j < height; ++j) {
    auto pointer = (png_const_bytep)ptr;
    pointer += j * pitch;
    if (setjmp(png_jmpbuf(png_ptr)))
      throw std::runtime_error("Unable to append more data to PNG buffer");

    png_write_row(png_ptr, pointer);
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    throw std::runtime_error(
        "Unable to write the end of the data to PNG buffer");
  }

  png_write_end(png_ptr, nullptr);
  png_destroy_write_struct(&png_ptr, &png_info_ptr);
  screen_buffer.type = image_data_t::image_type_e::png;
}

} // namespace qadx
/*
 * Copyright © 2022 Codethink Ltd.
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

#include <spdlog/spdlog.h>
#include <vector>

#define QAD_CHECK_ERR(expression)                                              \
  {                                                                            \
    if (!(expression)) {                                                       \
      spdlog::error("error: {} {}, line: {}, file: {}", (expression),          \
                    strerror(errno), __LINE__, __FILE__);                      \
      throw std::runtime_error("");                                            \
    }                                                                          \
  }

#define BUFFER_TYPE_PNG 0
#define BUFFER_TYPE_BMP 1

namespace qad {
using qad_screen_buffer_t = std::vector<unsigned char>;

template <typename DerivedType> struct qad_backend_input_t {
  qad_backend_input_t() = default;
  bool move(int const x_axis, int const y_axis, int const event) {
    return static_cast<DerivedType *>(this)->move_impl(x_axis, y_axis, event);
  }
  bool button(int const x, int const y) {
    return static_cast<DerivedType *>(this)->button_impl(x, y);
  }
  bool touch(int const x, int const y, int const duration, int const event) {
    return static_cast<DerivedType>(this)->touch_impl(x, y, duration, event);
  }
  bool swipe(int const x1, int const y1, int const x2, int const y2,
             int const velocity, int const event) {
    return static_cast<DerivedType>(this)->swipe_impl(x1, y1, x2, y2, velocity,
                                                      event);
  }
  bool key(int const key, int const event) {
    return static_cast<DerivedType>(this)->key_impl(key, event);
  }
  bool text(std::vector<int> const &key_codes, int const event) {
    return static_cast<DerivedType>(this)->text_impl(key_codes, event);
  }
};

template <typename Derived> struct qad_backend_screen_t {
  int grab_fb(qad_screen_buffer_t const &buffer, int const screen) {
    return static_cast<Derived>(this)->grab_fb(buffer, screen);
  }
  void list_fbs(char const *reply) {
    static_cast<Derived>(this)->list_fbs(reply);
  }
};

template <typename T> struct qad_backend_t {
  qad_backend_screen_t<T> screen_backend;
  qad_backend_input_t<T> input_backend;
};

enum class backend_type_e {
  ev_dev,
  uinput,
};
} // namespace qad
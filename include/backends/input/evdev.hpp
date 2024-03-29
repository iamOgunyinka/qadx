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

#include "backends/input/common.hpp"
#include "base_input.hpp"
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <memory>
#include <stdexcept>
#include <string>

#define QAD_CHECK_BOOL_ERR(expression)                                         \
  {                                                                            \
    if (!(expression)) {                                                       \
      fprintf(stderr, "error: %d %s, line: %d, file %s", (expression),         \
              strerror(errno), __LINE__, __FILE__);                            \
      fflush(stderr);                                                          \
      throw std::runtime_error("");                                            \
    }                                                                          \
  }

namespace qadx {

using namespace qadx::utils;
using qad_screen_buffer_t = std::vector<unsigned char>;

class ev_dev_backend_t final : public base_input_t {
  ev_dev_backend_t() = default;

public:
  ~ev_dev_backend_t() override = default;

  static ev_dev_backend_t *create_global_instance() {
    static std::unique_ptr<ev_dev_backend_t> ptr(new ev_dev_backend_t());
    return ptr.get();
  }

  bool move(int const x_axis, int const y_axis, int const event) final {
    auto_close_fd_t file_descriptor(create_file_descriptor(event));
    return send_position_event_mt(x_axis, y_axis, file_descriptor) &&
           send_syn_event(file_descriptor);
  }

  bool button(int const value, int const event) final {
    auto_close_fd_t file_descriptor(create_file_descriptor(event));
    int tracking_event = 100;
    if (value == 0)
      tracking_event = -1;

    return send_tracking_event(tracking_event, file_descriptor) &&
           send_button_event(value, file_descriptor) &&
           send_syn_event(file_descriptor);
  }

  bool touch(int const x, int const y, int const duration,
             int const event) final {
    auto_close_fd_t file_descriptor(create_file_descriptor(event));
    return send_touch(x, y, duration, event);
  }

  bool swipe(int const x1, int const y1, int const x2, int const y2,
             int const velocity, int const event) final {
    auto_close_fd_t file_descriptor(create_file_descriptor(event));
    return send_swipe(x1, y1, x2, y2, velocity, file_descriptor);
  }

  bool key(int const key, int const event) final {
    auto_close_fd_t fd(create_file_descriptor(event));
    QAD_CHECK_BOOL_ERR(send_key_event(key, fd))
    return send_syn_event(fd);
  }

  bool text(std::vector<int> const &key_codes, int const event) final {
    auto_close_fd_t fd(create_file_descriptor(event));
    QAD_CHECK_BOOL_ERR(send_text_event(key_codes, fd))
    return true;
  }

private:
  static inline int create_file_descriptor(int const event) {
    auto const event_location = "/dev/input/event" + std::to_string(event);

    if (int const file_descriptor = open(event_location.c_str(), O_RDWR);
        file_descriptor > 0) {
      return file_descriptor;
    }

    int const err = errno;
    auto const error_string = std::string("Could not open file ") +
                              event_location + ": " + strerror(err);
    throw std::runtime_error(error_string);
  }
};
} // namespace qadx

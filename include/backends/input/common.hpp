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

#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#define BUTTON_DOWN 1
#define BUTTON_UP 0

namespace qad {
namespace utils {
bool send_syn_event(int fd);
bool send_button_event(int value, int event);
bool send_pressure_event(int value, int event);
bool send_major_event(int value, int event);
bool send_position_event_abs(int x, int y, int event);
bool send_position_event_mt(int x, int y, int event);
bool send_tracking_event(int value, int event);
bool send_swipe_header(int major_value, int pressure, int event);
bool send_swipe_footer(int event);
bool send_touch(int x, int y, int duration, int event);
bool send_swipe(int x, int y, int x_2, int y_2, int v, int event);
bool send_key_event(int key, int fd);
bool send_text_event(std::vector<int> const &key_codes, int fd);
} // namespace utils

class auto_close_fd_t {
  int m_fd;

public:
  auto_close_fd_t() : m_fd(-1) {}
  explicit auto_close_fd_t(int const file_descriptor) : m_fd{file_descriptor} {}
  operator int() const { return m_fd; };
  auto_close_fd_t &operator=(int const fd) {
    if (m_fd != fd)
      m_fd = fd;
    return *this;
  }

  ~auto_close_fd_t() {
    if (m_fd > 0)
      close(m_fd);
  }
};
struct mmap_auto_free_t {
  explicit mmap_auto_free_t(void *memory, size_t const size)
      : m_memory(memory), m_size(size) {}
  ~mmap_auto_free_t() { munmap(m_memory, m_size); }
  void *m_memory = nullptr;
  size_t m_size = 0;
};

} // namespace qad

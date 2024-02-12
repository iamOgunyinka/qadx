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

#include "backends/input/evdev.hpp"
#include "backends/input/uinput.hpp"
#include "image.hpp"
#include <ivi-layermanagement-api/ilmControl/ivi-wm-client-protocol.h>
#include <memory>
#include <vector>
#include <wayland-client.h>

namespace qad {

struct wayland_screen_t {
  wl_output *output = nullptr;
  ivi_wm_screen *wm_screen = nullptr;
  int width = 0;
  int height = 0;
  int offset_x = 0;
  int offset_y = 0;
  int screen_id = 0;
  wl_list link{};
};

struct wayland_data_t {
  wl_display *display = nullptr;
  wl_event_queue *queue = nullptr;
  wl_registry *registry = nullptr;
  ivi_wm *wm = nullptr;
  wl_list output_list{};
};

struct screenshot_t {
  qad_screen_buffer_t data{};
  int width = 0;
  int height = 0;
  int stride = 0;
  int done = 0;
};

struct ilm_screen_t {
  std::string list_screens() { return {}; }
  bool grab_frame_buffer(image_data_t &screen_buffer, int screen);
  static ilm_screen_t create_instance();
  ~ilm_screen_t();

private:
  ilm_screen_t() = default;
  wayland_data_t wayland_data{};
};
} // namespace qad
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

#include "backends/input/common.hpp"
#include <algorithm>
#include <linux/input.h>
#include <thread>
#include <unistd.h>

namespace qadx::utils {
bool send_syn_event(int const fd) {
  input_event syn{};
  syn.input_event_sec = 0;
  syn.input_event_usec = 0;
  syn.type = EV_SYN;
  syn.code = SYN_REPORT;
  syn.value = 0;
  return write(fd, &syn, sizeof syn) != -1;
}

bool send_button_event(int const value, int const fd) {
  input_event button_event{};
  button_event.input_event_sec = 0;
  button_event.input_event_usec = 0;
  button_event.type = EV_KEY;
  button_event.code = BTN_TOUCH;
  button_event.value = value;
  return write(fd, &button_event, sizeof button_event) != -1;
}

bool send_key_event(int const key, int const fd) {
  input_event key_event{};
  key_event.input_event_sec = key_event.input_event_usec = 0;
  key_event.type = EV_KEY;
  key_event.code = key;
  key_event.value = 1;

  if (auto const error = write(fd, &key_event, sizeof key_event); error < 0)
    return false;

  key_event.type = EV_KEY;
  key_event.code = key;
  key_event.value = 0;

  return write(fd, &key_event, sizeof(key_event)) != -1;
}

bool send_text_event(std::vector<int> const &key_codes, int const fd) {
  return std::all_of(
      key_codes.begin(), key_codes.end(), [fd](auto const key_code) {
        if (!(send_key_event(key_code, fd) && send_syn_event(fd)))
          return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        return true;
      });
}

bool send_pressure_event(int const value, int const fd) {
  input_event pressure_event{};
  pressure_event.input_event_sec = 0;
  pressure_event.input_event_usec = 0;
  pressure_event.type = EV_ABS;
  pressure_event.code = ABS_MT_PRESSURE;
  pressure_event.value = value;
  return write(fd, &pressure_event, sizeof pressure_event) != -1;
}

bool send_major_event(int const value, int const fd) {
  input_event major_event{};
  major_event.input_event_sec = 0;
  major_event.input_event_usec = 0;
  major_event.type = EV_ABS;
  major_event.code = ABS_MT_TOUCH_MAJOR;
  major_event.value = value;

  if (write(fd, &major_event, sizeof major_event) < 0)
    return false;

  major_event.code = ABS_MT_WIDTH_MAJOR;
  return write(fd, &major_event, sizeof major_event) != -1;
}

bool send_position_event_abs(int const x, int const y, int const fd) {
  input_event position_event{};
  position_event.input_event_sec = 0;
  position_event.input_event_usec = 0;
  position_event.type = EV_ABS;
  position_event.code = ABS_X;
  position_event.value = x;

  if (write(fd, &position_event, sizeof position_event) < 0)
    return false;

  position_event.code = ABS_Y;
  position_event.value = y;

  return write(fd, &position_event, sizeof position_event) != -1;
}

bool send_position_event_mt(int const x, int const y, int const fd) {
  input_event position_event{};
  position_event.input_event_sec = 0;
  position_event.input_event_usec = 0;
  position_event.type = EV_ABS;
  position_event.code = ABS_MT_POSITION_X;
  position_event.value = x;

  if (write(fd, &position_event, sizeof position_event) < 0)
    return false;

  position_event.code = ABS_MT_POSITION_Y;
  position_event.value = y;

  return write(fd, &position_event, sizeof(position_event)) != -1;
}

bool send_position_event_rel(int const x, int const y, int const fd) {
  input_event position_event{};
  position_event.input_event_sec = 0;
  position_event.input_event_usec = 0;
  position_event.type = EV_REL;
  position_event.code = REL_X;
  position_event.value = x;

  if (write(fd, &position_event, sizeof position_event) < 0)
    return false;

  position_event.code = REL_Y;
  position_event.value = y;

  return write(fd, &position_event, sizeof(position_event)) != -1;
}

bool send_tracking_event(int const value, int const fd) {
  input_event tracking_event{};
  tracking_event.input_event_sec = 0;
  tracking_event.input_event_usec = 0;
  tracking_event.type = EV_ABS;
  tracking_event.code = ABS_MT_TRACKING_ID;
  tracking_event.value = value;

  return write(fd, &tracking_event, sizeof tracking_event) != -1;
}

bool send_swipe_header(int major_value, int pressure, int fd) {
  if (!send_major_event(major_value, fd))
    return false;

  return send_pressure_event(pressure, fd);
}

bool send_swipe_footer(int const fd) {
  return send_major_event(0, fd) && send_pressure_event(0, fd) &&
         send_tracking_event(-1, fd) && send_button_event(BUTTON_UP, fd) &&
         send_syn_event(fd);
}

bool send_touch(int const x, int const y, int const duration, int const fd) {
  bool const success = send_tracking_event(100, fd) &&
                       send_position_event_mt(x, y, fd) &&
                       send_button_event(BUTTON_DOWN, fd) &&
                       send_position_event_abs(x, y, fd) && send_syn_event(fd);
  if (!success)
    return success;

  if (duration > 0)
    std::this_thread::sleep_for(std::chrono::seconds(duration));

  return send_tracking_event(-1, fd) && send_button_event(BUTTON_UP, fd) &&
         send_syn_event(fd);
}

bool send_swipe(int x, int y, int const x2, int const y2, int const v,
                int const fd) {
  int const steps_y = (y - y2) / v * -1;
  int const steps_x = (x - x2) / v * -1;
  int const pressure = 50;
  int const tracking_event = 100;

  int major_value = 2;
  bool success = send_swipe_header(major_value, pressure, fd) &&
                 send_position_event_mt(x, y, fd) &&
                 send_tracking_event(tracking_event, fd) &&
                 send_button_event(BUTTON_DOWN, fd) && send_syn_event(fd);
  if (!success)
    return success;

  for (int i = 0; i < v; ++i) {
    success = send_major_event(major_value++, fd) &&
              send_pressure_event(pressure, fd) &&
              send_tracking_event(tracking_event, fd) &&
              send_position_event_mt(x, y, fd) && send_syn_event(fd);
    if (!success)
      return success;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    x += steps_x;
    y += steps_y;
  }

  return send_major_event(major_value, fd) &&
         send_pressure_event(pressure, fd) &&
         send_position_event_mt(x2, y2, fd) && send_syn_event(fd) &&
         send_swipe_footer(fd);
}
} // namespace qadx::utils

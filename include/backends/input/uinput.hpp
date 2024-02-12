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

#include "backend.hpp"
#include "backends/input/common.hpp"

#include <linux/uinput.h>

namespace qad {
using namespace qad::utils;

struct devices_t {
  int mouse{};
  int touch{};
  int keyboard{};
};

struct uinput_backend_t : qad_backend_t<uinput_backend_t> {
  uinput_backend_t() : qad_backend_t(), input_devices{} {
    input_devices.keyboard = create_keyboard();
    input_devices.mouse = create_mouse();
    input_devices.touch = create_touch_device();
  }

  ~uinput_backend_t() = default;

  bool move_impl(int x, int y, int event) {
    int &fd = get_uinput_file_descriptor(event);
    if (fd < 0)
      return false;
    if (!send_position_event_mt(x, y, fd)) {
      close(fd);
      return false;
    }
    return send_syn_event(fd);
  }

  bool button_impl(int value, int event) {
    int &fd = get_uinput_file_descriptor(event);
    if (fd < 0)
      return false;

    int const tracking_event = value == 0 ? -1 : 100;
    if (!(send_tracking_event(tracking_event, event) &&
          send_button_event(value, event))) {
      close(fd);
      return false;
    }

    return send_syn_event(event);
  }

  bool touch_impl(int x, int y, int duration, int event) {
    if (int &fd = get_uinput_file_descriptor(event); fd > 0) {
      bool const succeed = send_touch(x, y, duration, fd);
      if (!succeed)
        close(fd);
      return succeed;
    }
    return false;
  }

  bool swipe_impl(int x1, int y1, int x2, int y2, int velocity, int event) {
    if (int &fd = get_uinput_file_descriptor(event); fd > 0)
      return send_swipe(x1, y1, x2, y2, velocity, fd);
    return false;
  }

  bool key_impl(int key, int event) {
    if (int &fd = get_uinput_file_descriptor(event); fd > 0) {
      if (!send_key_event(key, fd)) {
        close(fd);
        return false;
      }
      return send_syn_event(fd);
    }
    return false;
  }

  bool text_impl(std::vector<int> const &key_codes, int const event) {
    if (int &fd = get_uinput_file_descriptor(event); fd > 0)
      return send_text_event(key_codes, fd);
    return false;
  }

private:
  static int create_mouse() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    /* enable mouse button left and relative events */
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);

    uinput_setup setup{};
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;  /* sample vendor */
    setup.id.product = 0x5678; /* sample product */
    strcpy(setup.name, "QAD mouse device");

    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);

    return fd;
  }

  [[nodiscard]] int &get_uinput_file_descriptor(int const event) {
    if (event == 0) {
      return input_devices.mouse;
    } else if (event == 1) {
      return input_devices.keyboard;
    } else if (event == 2) {
      return input_devices.touch;
    }
    throw std::runtime_error("event not found");
  }

  static int create_touch_device() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    /* enable abs touch events and mouse buttons */
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_PRESSURE);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);

    uinput_user_dev setup{};
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    strcpy(setup.name, "QAD touchinput device");

    setup.absmin[ABS_X] = 0;
    setup.absmax[ABS_X] = 32767;
    setup.absmin[ABS_Y] = 0;
    setup.absmax[ABS_Y] = 32767;
    setup.absmin[ABS_MT_POSITION_X] = 0;
    setup.absmax[ABS_MT_POSITION_X] = 32767;
    setup.absmin[ABS_MT_POSITION_Y] = 0;
    setup.absmax[ABS_MT_POSITION_Y] = 32767;
    setup.absmin[ABS_MT_PRESSURE] = 0;
    setup.absmax[ABS_MT_PRESSURE] = 100;

    if (write(fd, &setup, sizeof(setup)) < 0)
      return -1;

    if (ioctl(fd, UI_DEV_CREATE) < 0)
      return -1;

    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);

    return fd;
  }

  static int create_keyboard() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    QAD_CHECK_ERR(fd)
    /* enable key input events */
    QAD_CHECK_ERR(ioctl(fd, UI_SET_EVBIT, EV_KEY))

    for (int i = KEY_ESC; i <= KEY_RIGHT; i++)
      ioctl(fd, UI_SET_KEYBIT, i);

    uinput_setup setup{};
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;  /* sample vendor */
    setup.id.product = 0x5678; /* sample product */
    strcpy(setup.name, "QAD keyboard device");

    QAD_CHECK_ERR(ioctl(fd, UI_DEV_SETUP, &setup))
    QAD_CHECK_ERR(ioctl(fd, UI_DEV_CREATE))

    return fd;
  }

  devices_t input_devices{};
};
} // namespace qad

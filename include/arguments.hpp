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

#include "enumerations.hpp"
#include <optional>
#include <string>
#include <vector>

namespace qadx {
struct cli_args_t {
  int port = 3465;
  bool kms_format_rgb = false;
  bool guess_devices = false;
  bool verbose = false;
  std::string input_type = "uinput";
  std::string screen_backend = "kms";
};

struct input_device_mapping_t {
  int event_number = 0;
  int relevance = 0;
  input_device_type_e device_type = input_device_type_e::none;
};

using input_device_list_t = std::vector<input_device_mapping_t>;

struct runtime_args_t {
  bool kms_format_rgb = false;
  bool verbose = false;
  int port = 0;
  screen_type_e screen_backend = screen_type_e::none;
  input_type_e input_backend = input_type_e::none;
  std::vector<std::string> kms_backend_cards;
  std::optional<input_device_list_t> devices = std::nullopt;
};

int event_id_for(input_device_list_t const &device_list,
                 input_device_type_e type);
} // namespace qadx
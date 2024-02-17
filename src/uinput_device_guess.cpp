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

#include "arguments.hpp"
#include "string_utils.hpp"
#include <boost/algorithm/string/erase.hpp>
#include <boost/process.hpp>
#include <map>
#include <spdlog/spdlog.h>

namespace qadx {
namespace bp = boost::process;
[[nodiscard]] std::string
device_type_to_string(input_device_type_e const type) {
  switch (type) {
  case input_device_type_e::keyboard:
    return "Keyboard";
  case input_device_type_e::mouse:
    return "Mouse";
  case input_device_type_e::touchscreen:
    return "Touch";
  case input_device_type_e::trackpad:
    return "Trackpad";
  default:
    return "Unknown";
  }
}
[[nodiscard]] std::string get_device_value(std::string const &line,
                                           std::string const &expected_key) {
  static auto is_apostrophe = [](auto const ch) { return ch == '"'; };
  auto const view = boost::string_view(line.data() + 3, line.size() - 3);
  auto result = utils::split_string_view(view, "=");
  if (result.size() < 2 || utils::trim_copy(result[0]) != expected_key)
    throw std::runtime_error("Invalid line");

  result.erase(result.begin());

  auto value = boost::algorithm::join(result, "");
  utils::trim(value);

  while (boost::starts_with(value, "\""))
    boost::trim_left_if(value, is_apostrophe);
  while (boost::ends_with(value, "\""))
    boost::trim_right_if(value, is_apostrophe);

  utils::trim(value);
  return value;
}

void sort_devices(uinput_device_list_t &device_list) {
  std::sort(device_list.begin(), device_list.end(),
            [](auto const &a, auto const &b) {
              return std::tie(a.event_number, a.relevance) <
                     std::tie(b.event_number, b.relevance);
            });
}

void show_device_information(uinput_device_list_t const &device_list) {
  for (auto const &device_info : device_list) {
    spdlog::info("'{}' event on id '{}'",
                 device_type_to_string(device_info.device_type),
                 device_info.event_number);
  }
}

void gather_uinput_device_information(runtime_args_t &rt_args) {
  auto &devices = rt_args.devices.emplace();
  devices.push_back({0, 1, input_device_type_e::mouse});
  devices.push_back({1, 1, input_device_type_e::keyboard});
  devices.push_back({2, 1, input_device_type_e::touchscreen});

  if (rt_args.verbose)
    show_device_information(devices);
  sort_devices(devices);
}

input_device_type_e guess_device_type(std::string name) {
  utils::to_lower_string(name);
  if (name.find("keyboard") != std::string::npos) {
    return input_device_type_e::keyboard;
  } else if (name.find("mouse") != std::string::npos)
    return input_device_type_e::mouse;
  else if (name.find("touchpad") != std::string::npos)
    return input_device_type_e::trackpad;
  else if (name.find("touchinput") != std::string::npos)
    return input_device_type_e::touchscreen;
  return input_device_type_e::none;
}

void gather_evdev_device_information(runtime_args_t &rt_args) {
  bp::ipstream output_stream{};
  bp::child ch("cat /proc/bus/input/devices", bp::std_out > output_stream);

  std::string generic_device_name{};
  std::string physical_input{};

  auto &device_list = rt_args.devices.emplace();
  std::map<input_device_type_e, int> input_device_relevance{};

  for (std::string line{}; std::getline(output_stream, line);) {
    utils::trim(line);
    if (line.empty())
      continue;
    if (boost::starts_with(line, "N:"))
      generic_device_name = get_device_value(line, "Name");
    else if (boost::starts_with(line, "S:"))
      physical_input = get_device_value(line, "Sysfs");

    if (physical_input.length() > 0 && generic_device_name.length() > 0) {
      auto res = utils::split_string_view(physical_input, "/");
      if (!res.empty() && boost::starts_with(res.back(), "input")) {
        // magic number '5' below is the length of the prefix string "input"
        boost::erase_head(res.back(), 5);
        int const device_id = std::stoi(res.back());
        input_device_type_e const type = guess_device_type(generic_device_name);
        if (type != input_device_type_e::none) {
          int const count = ++input_device_relevance[type];
          device_list.push_back({device_id, count, type});
        }
      }

      physical_input.clear();
      generic_device_name.clear();
    }
  }
  ch.wait();

  if (device_list.empty())
    return rt_args.devices.reset();

  sort_devices(device_list);
  if (rt_args.verbose)
    show_device_information(device_list);
}
} // namespace qadx

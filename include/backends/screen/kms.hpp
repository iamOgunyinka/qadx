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

#include "base_screen.hpp"
#include <memory>

namespace qadx {
namespace details {
struct kms_screen_crtc_t {
  uint32_t id = 0;
  int valid_mode = 0;
};
} // namespace details

using string_list_t = std::vector<std::string>;

struct kms_screen_t final : public base_screen_t {
  static kms_screen_t *
  create_global_instance(string_list_t const &backend_cards,
                         int kms_format_rgb);

  std::string list_screens() final;
  bool grab_frame_buffer(image_data_t &screen_buffer, int screen) final;

  ~kms_screen_t() override = default;

  static std::vector<details::kms_screen_crtc_t>
  list_screens_impl(std::string const &card);

private:
  friend std::unique_ptr<kms_screen_t>
  create_instance(string_list_t const &backend_cards, int kms_format_rgb);
  friend std::string select_suitable_kms_card(string_list_t const &cards,
                                              int use_rgb);
  kms_screen_t() : base_screen_t() {}
  std::string m_card = "/dev/dri/";
};
} // namespace qadx
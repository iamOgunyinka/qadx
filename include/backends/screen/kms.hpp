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
#include "base_screen.hpp"
#include <memory>

namespace qadx {
struct kms_screen_t final : public base_screen_t {
  static std::shared_ptr<kms_screen_t>
  create_global_instance(std::string const &backend_card, int kms_format_rgb);

  std::string list_screens() final;
  bool grab_frame_buffer(image_data_t &screen_buffer, int screen) final;
  ~kms_screen_t() override = default;

private:
  friend std::unique_ptr<kms_screen_t>
  create_instance(std::string const &backend_card, int kms_format_rgb);
  kms_screen_t() : base_screen_t() {}
  int m_colorModel = 0;
  std::string m_card = "/dev/dri/";
};
} // namespace qadx
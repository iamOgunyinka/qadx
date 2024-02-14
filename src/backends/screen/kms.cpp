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

#include "backends/screen/kms.hpp"
#include "backends/input/common.hpp"
#include "drm_mode.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <memory>

namespace qadx {
std::string kms_screen_t::list_screens() {
  int file_descriptor = open(m_card.c_str(), O_RDONLY);
  spdlog::info("FileDescriptor opening for {} {}", m_card, file_descriptor);

  if (file_descriptor < 0) {
    spdlog::error("Error opening {}: {}", m_card, strerror(errno));
    return {};
  }

  auto resources = drmModeGetResources(file_descriptor);
  if (!resources) {
    close(file_descriptor);
    spdlog::error("Error getting display config: {}", strerror(errno));
    spdlog::error("Is DRM device set correctly?");
    return {};
  }

  std::string reply{};
  for (int i = 0; i < resources->count_crtcs; ++i) {
    // A CRTC is simply an object that can scan out a framebuffer to a
    // display sink, and contains mode timing and relative position
    // information.
    auto crtc = drmModeGetCrtc(file_descriptor, resources->crtcs[i]);
    if (!crtc) {
      spdlog::warn("Error getting CRTC '{}': {}", resources->crtcs[i],
                   strerror(errno));
      continue;
    }
    reply += fmt::format("CRTC: ID={}, mode_valid={}\n", crtc->crtc_id,
                         crtc->mode_valid);
    drmModeFreeCrtc(crtc);
  }
  drmModeFreeResources(resources);
  close(file_descriptor);
  return reply;
}

bool kms_screen_t::grab_frame_buffer(image_data_t &screen_buffer,
                                     int const screen) {
  int file_descriptor = open(m_card.c_str(), O_RDWR | O_CLOEXEC);
  if (file_descriptor < 0) {
    spdlog::error("Error opening {}: {}", m_card, strerror(errno));
    return false;
  }

  auto crtc = drmModeGetCrtc(file_descriptor, screen);
  if (!crtc) {
    close(file_descriptor);
    spdlog::error("Error getting CRTC '{}': {}", screen, strerror(errno));
    return false;
  }
  drmModeFB *fb = drmModeGetFB(file_descriptor, crtc->buffer_id);
  if (!fb) {
    close(file_descriptor);
    drmModeFreeCrtc(crtc);
    spdlog::error("Error getting frame buffer '{}': {}", crtc->buffer_id,
                  strerror(errno));
    return false;
  }

  auto const fb_size = fb->pitch * fb->height;
  drm_mode_map_dumb dumb_map{};
  dumb_map.handle = fb->handle;
  dumb_map.offset = 0;

  drmIoctl(file_descriptor, DRM_IOCTL_MODE_MAP_DUMB, &dumb_map);
  auto ptr = mmap(nullptr, fb_size, PROT_READ, MAP_SHARED, file_descriptor,
                  __off_t(dumb_map.offset));
  mmap_auto_free_t auto_free(ptr, fb_size);
  write_png(ptr, (int)fb->width, int(fb->height), int(fb->pitch), (int)fb->bpp,
            0, screen_buffer);
  drmModeFreeFB(fb);
  drmModeFreeCrtc(crtc);
  close(file_descriptor);
  return true;
}

std::unique_ptr<kms_screen_t> create_instance(std::string const &backend_card,
                                              int const kms_format_rgb) {
  kms_screen_t kms_screen{};
  kms_screen.m_card += backend_card;
  kms_screen.m_colorModel = kms_format_rgb;

  int file_descriptor = open(kms_screen.m_card.c_str(), O_RDWR | O_CLOEXEC);
  if (file_descriptor < 0) {
    spdlog::error("Failed to open {}", kms_screen.m_card);
    return nullptr;
  }
  close(file_descriptor);
  return std::make_unique<kms_screen_t>(kms_screen);
}

std::shared_ptr<kms_screen_t>
kms_screen_t::create_global_instance(std::string const &backend_card,
                                     int const kms_format_rgb) {
  static std::shared_ptr<kms_screen_t> screen{
      create_instance(backend_card, kms_format_rgb)};
  return screen;
}
} // namespace qadx
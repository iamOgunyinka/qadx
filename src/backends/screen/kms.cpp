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
#ifdef QADX_USE_WITH_WEBSOCKET
#include "backends/screen/kms_page_flip.hpp"
#else
#include <optional>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif
#include <drm_mode.h>
#include <spdlog/spdlog.h>

namespace qadx {

std::vector<details::kms_screen_crtc_t>
kms_screen_t::list_screens_impl(std::string const &card) {
  int file_descriptor = open(card.c_str(), O_RDONLY);
  if (file_descriptor < 0) {
    spdlog::error("Error opening {}: {}", card, strerror(errno));
    return {};
  }

  auto resources = drmModeGetResources(file_descriptor);
  if (!resources) {
    close(file_descriptor);
    spdlog::error("Error getting display config: {}", strerror(errno));
    spdlog::error("Is DRM device set correctly?");
    return {};
  }
  std::vector<details::kms_screen_crtc_t> screens{};
  int const count = resources->count_crtcs;
  if (count > 0)
    screens.reserve(count);
  for (int i = 0; i < count; ++i) {
    // A CRTC is simply an object that can scan out a framebuffer to a
    // display sink, and contains mode timing and relative position
    // information.
    auto crtc = drmModeGetCrtc(file_descriptor, resources->crtcs[i]);
    if (!crtc) {
      spdlog::warn("Error getting CRTC '{}': {}", resources->crtcs[i],
                   strerror(errno));
      continue;
    }
    screens.push_back({crtc->crtc_id, crtc->mode_valid});
    drmModeFreeCrtc(crtc);
  }
  drmModeFreeResources(resources);
  close(file_descriptor);
  return screens;
}

std::string kms_screen_t::list_screens() {
  std::string reply{};
  if (auto const screens = list_screens_impl(m_card); !screens.empty()) {
    for (auto const &screen_info : screens) {
      reply += fmt::format("CRTC: ID={}, mode_valid={}\n", screen_info.id,
                           screen_info.valid_mode);
    }
  }
  return reply;
}

bool kms_screen_t::grab_frame_buffer(image_data_t &screen_buffer,
                                     int const screen_id) {
  int const file_descriptor = open(m_card.c_str(), O_RDWR | O_CLOEXEC);
  if (file_descriptor < 0) {
    spdlog::error("Error opening {}: {}", m_card, strerror(errno));
    return false;
  }

  auto crtc = drmModeGetCrtc(file_descriptor, screen_id);
  if (!crtc) {
    close(file_descriptor);
    spdlog::error("Error getting CRTC '{}': {}", screen_id, strerror(errno));
    return false;
  }

  auto fb = drmModeGetFB(file_descriptor, crtc->buffer_id);
  if (!fb) {
    close(file_descriptor);
    drmModeFreeCrtc(crtc);
    spdlog::error("Error getting frame buffer '{}': {}", crtc->buffer_id,
                  strerror(errno));
    return false;
  }

  drm_mode_map_dumb dumb_map{};
  dumb_map.handle = fb->handle;
  dumb_map.offset = 0;

  drmIoctl(file_descriptor, DRM_IOCTL_MODE_MAP_DUMB, &dumb_map);
  auto const fb_size = fb->pitch * fb->height;
  auto ptr = mmap(nullptr, fb_size, PROT_READ, MAP_SHARED, file_descriptor,
                  __off_t(dumb_map.offset));
  if (ptr == MAP_FAILED) {
    drmModeFreeCrtc(crtc);
    drmModeFreeFB(fb);
    close(file_descriptor);
    spdlog::error("Memory mapping failed");
    return false;
  }

  mmap_auto_free_t auto_free(ptr, fb_size);
  write_png(ptr, (int)fb->width, int(fb->height), int(fb->pitch), (int)fb->bpp,
            0, screen_buffer);
  drmModeFreeFB(fb);
  drmModeFreeCrtc(crtc);
  close(file_descriptor);
  return true;
}

std::optional<details::kms_screen_crtc_t>
find_usable_screen(std::string const &card) {
  auto const screens = kms_screen_t::list_screens_impl(card);
  auto valid_screen_iter =
      std::find_if(screens.begin(), screens.end(), [](auto const &screen_info) {
        return screen_info.valid_mode == 1;
      });
  if (valid_screen_iter != screens.end())
    return *valid_screen_iter;
  return std::nullopt;
}

std::string select_suitable_kms_card(string_list_t const &cards, int const) {
  for (auto const &card : cards) {
    int screen_id = 2;
    kms_screen_t kms_screen{};
    kms_screen.m_card += card;
    if (auto const sc = find_usable_screen(kms_screen.m_card); sc.has_value())
      screen_id = static_cast<int>(sc->id);

    image_data_t image{};
    if (kms_screen.grab_frame_buffer(image, screen_id))
      return card;
  }
  return {};
}

std::unique_ptr<kms_screen_t>
create_instance(string_list_t const &backend_cards, int const kms_format_rgb) {
  auto const card = select_suitable_kms_card(backend_cards, kms_format_rgb);
  if (card.empty())
    return nullptr;

  kms_screen_t kms_screen{};
  kms_screen.m_card += card;

  int file_descriptor = open(kms_screen.m_card.c_str(), O_RDWR | O_CLOEXEC);
  if (file_descriptor < 0) {
    spdlog::error("Failed to open {}", kms_screen.m_card);
    return nullptr;
  }
  close(file_descriptor);
  return std::make_unique<kms_screen_t>(kms_screen);
}

#ifdef QADX_USE_WITH_WEBSOCKET
void page_flip_background_thread(std::vector<std::string> const &backend_cards,
                                 int const kms_format_rgb) {
  auto const card_suffix =
      select_suitable_kms_card(backend_cards, kms_format_rgb);

  if (card_suffix.empty())
    return;

  auto const card = "/dev/dri/" + card_suffix;
  int const file_descriptor = open(card.c_str(), O_RDWR | O_CLOEXEC);
  if (file_descriptor < 0) {
    spdlog::error("Cannot open '{}' because '{}'", card, strerror(errno));
    return;
  }

  // check that we have the capability to create dumb buffers
  uint64_t has_dumb_buffer_cap = 0;
  if (drmGetCap(file_descriptor, DRM_CAP_DUMB_BUFFER, &has_dumb_buffer_cap) <
          0 ||
      !has_dumb_buffer_cap) {
    close(file_descriptor);
    return spdlog::error(
        "DRM device does not have the capability to create dumb buffers");
  }

  auto page_flip_data = new page_flip_drm_t();
  page_flip_data->file_descriptor = file_descriptor;
  if (!details::create_frame_buffers(file_descriptor, *page_flip_data)) {
    delete page_flip_data;
    return;
  }

  auto &buffer = page_flip_data->buffers[0];
  spdlog::info("FD: {}, CRTC-ID: {}, BID: {}, CID: {}", file_descriptor,
               page_flip_data->crtc_id, buffer.buffer_id,
               page_flip_data->connector_id);
  spdlog::info("ID: {}, P: {}, H: {}, S: {}, HP: {}", buffer.buffer_id,
               buffer.pitch, buffer.buffer_handle, buffer.buffer_size,
               buffer.has_pending_flip);

  auto ret = drmSetMaster(file_descriptor);
  if (ret) {
    delete page_flip_data;
    return spdlog::error("unable to switch to master mode, {}",
                         strerror(errno));
  }

  ret = drmModeSetCrtc(file_descriptor, page_flip_data->crtc_id,
                       buffer.buffer_id, 0, 0, &page_flip_data->connector_id, 1,
                       &page_flip_data->mode);
  if (ret) { // todo: do proper resource cleanup
    delete page_flip_data;
    return spdlog::error("unable to set crtc mode on buffer, {}",
                         strerror(errno));
  }

  ret = drmDropMaster(file_descriptor);
  if (ret) {
    delete page_flip_data;
    return spdlog::error("unable to drop from master mode {}", strerror(errno));
  }

  page_flip_data->event_context.version = 3;
  page_flip_data->event_context.page_flip_handler = details::page_flip_callback;

  // let's start with the first buffer, then we flip the buffer on each write
  ret = drmModePageFlip(file_descriptor, page_flip_data->crtc_id,
                        buffer.buffer_id, DRM_MODE_PAGE_FLIP_EVENT,
                        page_flip_data);
  if (ret) {
    delete page_flip_data;
    return;
  }

  page_flip_data->file_descriptor = file_descriptor;
  buffer.has_pending_flip = 1;

  std::thread{[page_flip_data] {
    std::make_shared<async_kms_page_flit_handler_t>(get_io_context(),
                                                    page_flip_data)
        ->run();
    get_io_context().run();
  }}.detach();
}
#endif

kms_screen_t *
kms_screen_t::create_global_instance(string_list_t const &backend_cards,
                                     int const kms_format_rgb) {
  static auto screen = create_instance(backend_cards, kms_format_rgb);
#ifdef QADX_USE_WITH_WEBSOCKET
  static bool background_thread = [=] {
    page_flip_background_thread(backend_cards, kms_format_rgb);
    return true;
  }();
#endif
  return screen.get();
}

/*
dumb_map_auto_t::dumb_map_auto_t(int const fd)
    : m_fileDescriptor(fd), m_mapDumb{} {
  drm_mode_create_dumb dumb_buffer{};
  memset(&dumb_buffer, 0, sizeof dumb_buffer);
  dumb_buffer.bpp = 32; // bits per pixel
  dumb_buffer.width = ???;
  dumb_buffer.height = ???;
  auto ret =
      drmIoctl(m_fileDescriptor, DRM_IOCTL_MODE_CREATE_DUMB, &dumb_buffer);
  if (ret < 0) {
    spdlog::error("Unable to create a dumb buffer");
    return;
  }

  memset(&m_mapDumb, 0, sizeof m_mapDumb);
  m_mapDumb.handle = dumb_buffer.handle;
  ret = drmIoctl(m_fileDescriptor, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
  if (ret) {
    reset();
    spdlog::error("unable to map frame buffer");
    return;
  }
  m_constructed = true;
}

void dumb_map_auto_t::reset() {
  if (!m_constructed)
    return;
  drm_mode_destroy_dumb destroy_dumb{};
  destroy_dumb.handle = m_mapDumb.handle;
  drmIoctl(m_fileDescriptor, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
}
 */
} // namespace qadx

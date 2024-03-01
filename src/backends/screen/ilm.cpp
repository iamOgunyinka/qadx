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

#include "backends/screen/ilm.hpp"
#include "image.hpp"
#include <netinet/in.h>
#include <spdlog/spdlog.h>

namespace qadx {
void wm_screen_listener_screen_id(void *data, struct ivi_wm_screen *,
                                  uint32_t const screen_id) {
  auto output_screen = reinterpret_cast<wayland_screen_t *>(data);
  output_screen->screen_id = static_cast<int>(screen_id);
}

void wm_screen_listener_layer_added(void *, ivi_wm_screen *, uint32_t) {}
void wm_screen_listener_connector_name(void *, ivi_wm_screen *, char const *) {}
void wm_screen_listener_error(void *, ivi_wm_screen *, uint32_t, char const *) {
}

void output_handle_done(void *, wl_output *) {}
void output_handle_scale(void *, wl_output *, int32_t) {}
void output_handle_geometry(void *, wl_output *wl_output, int x, int const y,
                            int const, int const, int const, char const *,
                            char const *, int) {
  auto output_screen =
      reinterpret_cast<wayland_screen_t *>(wl_output_get_user_data(wl_output));
  if (wl_output == output_screen->output) {
    output_screen->offset_x = x;
    output_screen->offset_y = y;
  }
}

void output_handle_mode(void *, wl_output *wl_output, uint32_t const flags,
                        int const width, int const height, int const) {
  auto output_screen =
      reinterpret_cast<wayland_screen_t *>(wl_output_get_user_data(wl_output));
  if (wl_output == output_screen->output && (flags & WL_OUTPUT_MODE_CURRENT)) {
    output_screen->width = width;
    output_screen->height = height;
  }
}

wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
    output_handle_done,
    output_handle_scale,
};

ivi_wm_screen_listener const wm_screen_listener = {
    wm_screen_listener_screen_id,
    wm_screen_listener_layer_added,
    wm_screen_listener_connector_name,
    wm_screen_listener_error,
};

void registry_remover(void *, wl_registry *, uint32_t) {}
void registry_handler(void *data, wl_registry *registry, uint32_t const id,
                      char const *interface, uint32_t const) {
  auto wd = reinterpret_cast<wayland_data_t *>(data);
  if (interface == std::string("wl_output")) {
    auto output_screen = new wayland_screen_t();
    output_screen->output = reinterpret_cast<wl_output *>(
        wl_registry_bind(registry, id, &wl_output_interface, 1));

    wl_output_add_listener(output_screen->output, &output_listener,
                           output_screen);
    if (wd->wm) {
      output_screen->wm_screen =
          ivi_wm_create_screen(wd->wm, output_screen->output);
      ivi_wm_screen_add_listener(output_screen->wm_screen, &wm_screen_listener,
                                 output_screen);
    }
    wl_list_insert(&wd->output_list, &output_screen->wy_link);
  } else if (interface == std::string("ivi_wm")) {
    auto r = wl_registry_bind(registry, id, &ivi_wm_interface, 1);
    wd->wm = reinterpret_cast<ivi_wm *>(r);
  }
}

static wl_registry_listener const registry_listener = {
    registry_handler,
    registry_remover,
};

std::unique_ptr<ilm_screen_t> create_instance() {
  wayland_data_t wd{};

  wd.display = wl_display_connect(nullptr);
  if (!wd.display) {
    spdlog::error("failed to connect to WL display: {}", strerror(errno));
    return nullptr;
  }

  wd.queue = wl_display_create_queue(wd.display);
  wl_list_init(&wd.output_list);
  wd.registry = wl_display_get_registry(wd.display);
  wl_proxy_set_queue((wl_proxy *)wd.registry, wd.queue);
  wl_registry_add_listener(wd.registry, &registry_listener, &wd);

  // Block until all pending request are processed by the server
  if (wl_display_roundtrip_queue(wd.display, wd.queue) == -1) {
    spdlog::error("Failed to get globals: {}", strerror(errno));
    return nullptr;
  }

  if (!wd.wm) {
    spdlog::error("Compositor does not ivi_wm or weston screenshoter");
    wl_display_disconnect(wd.display);
    return nullptr;
  }

  wayland_screen_t *output = nullptr;
  wl_list_for_each(output, &wd.output_list, wy_link) {
    if (!output->wm_screen) {
      output->wm_screen = ivi_wm_create_screen(wd.wm, output->output);
      ivi_wm_screen_add_listener(output->wm_screen, &wm_screen_listener,
                                 output);
    }
  }

  // Block until all pending request are processed by the server
  if (wl_display_roundtrip_queue(wd.display, wd.queue) == -1) {
    // do error check and free resources
    spdlog::error("setting ivi_wm_screen listeners failed: {}",
                  strerror(errno));
    wl_display_disconnect(wd.display);
    return nullptr;
  }
  return std::unique_ptr<ilm_screen_t>(new ilm_screen_t(std::move(wd)));
}

ilm_screen_t *ilm_screen_t::create_global_instance() {
  static auto instance = create_instance();
  return instance.get();
}

void ivi_screenshot_done(void *data, ivi_screenshot *ivi_screenshot,
                         int32_t const fd, int32_t const width,
                         int32_t const height, int32_t const stride,
                         uint32_t const format, uint32_t const) {

  auto screen_shot = reinterpret_cast<screenshot_t *>(data);
  screen_shot->done = 1;
  ivi_screenshot_destroy(ivi_screenshot);

  int flip_order;
  int has_alpha;

  switch (format) {
  case WL_SHM_FORMAT_ARGB8888:
    flip_order = 1;
    has_alpha = 1;
    break;
  case WL_SHM_FORMAT_XRGB8888:
    flip_order = 1;
    has_alpha = 0;
    break;
  case WL_SHM_FORMAT_ABGR8888:
    flip_order = 0;
    has_alpha = 1;
    break;
  case WL_SHM_FORMAT_XBGR8888:
    flip_order = 0;
    has_alpha = 0;
    break;
  default:
    return spdlog::error("unsupported pixel format {}", format);
  }

  int const bytes_per_pixel = has_alpha ? 4 : 3;
  int32_t image_size = stride * height;

  auto raw_memory = mmap(nullptr, image_size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);

  mmap_auto_free_t auto_free(raw_memory, image_size);
  auto buffer = reinterpret_cast<unsigned char *>(raw_memory);

  if (buffer == MAP_FAILED)
    return spdlog::error("failed to mmap screen_shot file: {}", image_size);

  screen_shot->data.resize(image_size);
  if (screen_shot->data.empty()) {
    return spdlog::error("failed to allocate %d bytes for image buffer: {}",
                         image_size);
  }

  // Store the image in image_buffer in the following order B, G, R, [A](B at
  // the lowest address)
  for (int32_t row = 0; row < height; ++row) {
    for (int32_t col = 0; col < width; ++col) {
      int32_t const offset = (height - row - 1) * width + col;
      uint32_t const pixel = htonl(((uint32_t *)buffer)[offset]);
      auto pixel_p = (char *)&pixel;
      int32_t const image_offset = row * stride + col * bytes_per_pixel;
      for (int32_t i = 0; i < 3; ++i) {
        int32_t j = flip_order ? 2 - i : i;
        screen_shot->data[image_offset + i] = pixel_p[1 + j];
      }
      if (has_alpha)
        screen_shot->data[image_offset + 3] = pixel_p[0];
    }
  }
  screen_shot->height = height;
  screen_shot->width = width;
  screen_shot->stride = stride;
}

void ivi_screenshot_error(void *data, struct ivi_screenshot *ivi_screenshot,
                          uint32_t error, const char *message) {
  auto screenshot = reinterpret_cast<screenshot_t *>(data);
  ivi_screenshot_destroy(ivi_screenshot);
  spdlog::error("screenshot failed, error {}: {}", error, message);
  screenshot->done = 1;
}

ivi_screenshot_listener screenshot_listener = {
    ivi_screenshot_done,
    ivi_screenshot_error,
};

bool ilm_screen_t::grab_frame_buffer(image_data_t &screen_buffer,
                                     int const screen) {
  wayland_screen_t *chosen_screen = nullptr;
  wayland_screen_t *output = nullptr;

  wl_list_for_each(output, &wayland_data.output_list, wy_link) {
    if (output && screen == output->screen_id) {
      chosen_screen = output;
      break;
    }
  }

  if (!chosen_screen) {
    spdlog::error("Failed to find screen with ID {}", screen);
    return false;
  }

  // Grab screenshot of individual screens
  auto screen_shot_screen = ivi_wm_screen_screenshot(chosen_screen->wm_screen);
  if (!screen_shot_screen)
    return false;

  screenshot_t screen_shot{};
  ivi_screenshot_add_listener(screen_shot_screen, &screenshot_listener,
                              &screen_shot);

  int ret;
  do {
    ret = wl_display_roundtrip_queue(wayland_data.display, wayland_data.queue);
  } while ((ret != -1) && !screen_shot.done);

  if (screen_shot.data.empty()) {
    spdlog::error("Error taking screenshot");
    return false;
  }

  encode_bmp(screen_shot.data, screen_shot.width, screen_shot.height,
             screen_shot.stride, screen_buffer);
  return true;
}

ilm_screen_t::~ilm_screen_t() {
  if (!wayland_data.display)
    return;

  if (wl_list_empty(&wayland_data.output_list) == 0) {
    wayland_screen_t *output = nullptr;
    wayland_screen_t *next = nullptr;
    wl_list_for_each_safe(output, next, &wayland_data.output_list, wy_link) {
      if (output && output->wm_screen)
        ivi_wm_screen_destroy(output->wm_screen);
      if (output)
        wl_output_destroy(output->output);
      delete output;
    }
  }

  if (wayland_data.wm)
    ivi_wm_destroy(wayland_data.wm);

  if (wayland_data.registry)
    wl_registry_destroy(wayland_data.registry);
  if (wayland_data.queue)
    wl_event_queue_destroy(wayland_data.queue);
  wl_display_disconnect(wayland_data.display);
}
} // namespace qadx
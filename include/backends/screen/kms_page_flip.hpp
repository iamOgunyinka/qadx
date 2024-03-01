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

#include "image.hpp"
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <optional>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace qadx {
namespace net = boost::asio;
struct kms_screen_cache_t {
  uint32_t width{};
  uint32_t height{};
  uint32_t pitch{};
};

struct drm_frame_buffer_cache_t {
  void *mapped_memory = nullptr;
  uint32_t buffer_size = 0;
  uint32_t buffer_handle = 0;
  uint32_t buffer_id = 0;
  uint32_t pitch = 0;
  uint32_t has_pending_flip = 0; // true or false
};

struct page_flip_drm_t {
  uint32_t crtc_id = 0;
  uint32_t connector_id = 0;
  uint32_t height = 0;
  uint32_t width = 0;
  uint32_t activated_buffer = 0;
  int file_descriptor = 0;
  drmModeModeInfo mode{};
  drmEventContext event_context{};
  // these two buffers will be switched between page flips
  drm_frame_buffer_cache_t buffers[2]{};
  std::function<void()> parent_resume = nullptr;

  page_flip_drm_t() = default;
  ~page_flip_drm_t();
};

struct page_flip_base_t {
  net::io_context &m_ioContext;

  virtual image_data_t image() = 0;
  virtual void run() = 0;
  virtual void reset() = 0;
  virtual void set_next_timer() = 0;
  explicit page_flip_base_t(net::io_context &ioContext)
      : m_ioContext(ioContext) {}
  virtual ~page_flip_base_t() = default;
};

struct async_kms_page_flip_handler_t
    : page_flip_base_t,
      public std::enable_shared_from_this<async_kms_page_flip_handler_t> {
  page_flip_drm_t *m_pageFlipDrm = nullptr;
  std::optional<net::posix::stream_descriptor> m_streamDesc = std::nullopt;
  net::deadline_timer m_timer;
  std::mutex m_mutex{};

  async_kms_page_flip_handler_t(net::io_context &ioContext, page_flip_drm_t *p)
      : page_flip_base_t(ioContext), m_pageFlipDrm(p), m_timer(m_ioContext) {}
  ~async_kms_page_flip_handler_t() override { reset(); }
  image_data_t image() final;
  void run() override;

private:
  void on_ready_read(boost::system::error_code ec);
  void on_page_flip_occurred();
  void set_next_timer() override;
  void reset() override;
};

struct time_based_page_flip_handler_t
    : page_flip_base_t,
      public std::enable_shared_from_this<time_based_page_flip_handler_t> {

  struct internal_memory_t {
    void *ptr = nullptr;
    uint32_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pitch = 0;
    uint32_t bpp = 0;
  };

  std::optional<net::deadline_timer> m_timer = std::nullopt;
  std::mutex m_mutex{};
  _drmModeCrtc *m_crtc;
  internal_memory_t m_imageBuffer[2]{};
  int const m_fileDescriptor;
  size_t m_index;

  explicit time_based_page_flip_handler_t(net::io_context &ioContext, int fd,
                                          _drmModeCrtc *crtc)
      : page_flip_base_t(ioContext), m_timer(std::nullopt), m_crtc(crtc),
        m_fileDescriptor(fd), m_index(1) {}
  ~time_based_page_flip_handler_t() override { reset(); }
  image_data_t image() override;
  void run() override;

private:
  void set_next_timer() override;
  void reset() override;
  void take_screenshot();
};

// defined in server.cpp
net::io_context &get_io_context();

namespace details {
void page_flip_callback(int file_descriptor, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data);
[[nodiscard]] bool create_frame_buffers(int file_descriptor,
                                        page_flip_drm_t &page_flip_data);
} // namespace details
} // namespace qadx
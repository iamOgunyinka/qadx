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

#include "backends/screen/kms_page_flip.hpp"
#include <boost/asio/post.hpp>
#include <spdlog/spdlog.h>
#include <sys/mman.h>

namespace qadx {
using wait_type = net::posix::descriptor_base::wait_type;

namespace details {
[[nodiscard]] bool associate_connector_with_crtc(int const file_descriptor,
                                                 page_flip_drm_t &data) {
  auto resources = drmModeGetResources(file_descriptor);
  if (!resources)
    return false;

  for (int index = 0; index < resources->count_connectors; ++index) {
    auto connector =
        drmModeGetConnector(file_descriptor, resources->connectors[index]);
    if (!connector)
      continue;

    if (connector->connection != DRM_MODE_CONNECTED ||
        connector->count_modes == 0) {
      drmModeFreeConnector(connector);
      continue;
    }

    data.width = connector->modes[0].hdisplay;
    data.height = connector->modes[0].vdisplay;
    memcpy(&data.mode, &connector->modes[0], sizeof data.mode);

    if (connector->encoder_id) {
      auto encoder = drmModeGetEncoder(file_descriptor, connector->encoder_id);
      if (encoder) {
        if (encoder->crtc_id) {
          auto c = drmModeGetCrtc(file_descriptor, encoder->crtc_id);
          if (c && c->mode_valid) {
            data.crtc_id = encoder->crtc_id;
            data.connector_id = connector->connector_id;

            drmModeFreeCrtc(c);
            drmModeFreeEncoder(encoder);
            drmModeFreeConnector(connector);
            drmModeFreeResources(resources);
            return true;
          }
          if (c)
            drmModeFreeCrtc(c);
        }

        drmModeFreeEncoder(encoder);
      }
    }

    for (int x = 0; x < connector->count_encoders; ++x) {
      auto encoder = drmModeGetEncoder(file_descriptor, connector->encoders[x]);
      if (!encoder)
        continue;
      for (int y = 0; y < resources->count_crtcs; ++y) {
        // checks that this encoder with this CRT controller
        if (!(encoder->possible_crtcs & (1 << y)))
          continue;
        auto c = drmModeGetCrtc(file_descriptor, resources->crtcs[y]);
        if (c && c->mode_valid) {
          data.crtc_id = encoder->crtc_id;
          data.connector_id = connector->connector_id;

          drmModeFreeEncoder(encoder);
          drmModeFreeConnector(connector);
          drmModeFreeResources(resources);
          return true;
        }
        if (c)
          drmModeFreeCrtc(c);
      }
      drmModeFreeEncoder(encoder);
    }
    drmModeFreeConnector(connector);
  }
  drmModeFreeResources(resources);
  return false;
}

[[nodiscard]] bool create_frame_buffers(int const file_descriptor,
                                        page_flip_drm_t &page_flip_data) {
  if (!associate_connector_with_crtc(file_descriptor, page_flip_data))
    return false;

  for (auto &frame : page_flip_data.buffers) {
    drm_mode_create_dumb dumb_buffer{};
    memset(&dumb_buffer, 0, sizeof dumb_buffer);
    dumb_buffer.bpp = 32; // bits per pixel

    auto ret =
        drmIoctl(file_descriptor, DRM_IOCTL_MODE_CREATE_DUMB, &dumb_buffer);
    if (ret < 0) {
      spdlog::error("Unable to create a dumb buffer");
      return false;
    }

    frame.pitch = dumb_buffer.pitch;
    frame.buffer_size = dumb_buffer.size;
    frame.buffer_handle = dumb_buffer.handle;
    frame.buffer_id = 0;

    auto const depth = 24;
    drm_mode_destroy_dumb destroy_dumb{};

    ret = drmModeAddFB(file_descriptor, page_flip_data.width,
                       page_flip_data.height, depth, dumb_buffer.bpp,
                       frame.pitch, frame.buffer_handle, &frame.buffer_id);

    if (ret) {
      destroy_dumb.handle = frame.buffer_handle;
      drmIoctl(file_descriptor, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
      spdlog::error("unable to add frame buffer");
      return false;
    }

    drm_mode_map_dumb map_dumb{};
    memset(&map_dumb, 0, sizeof map_dumb);
    map_dumb.handle = frame.buffer_handle;
    ret = drmIoctl(file_descriptor, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
    if (ret) {
      drmModeRmFB(file_descriptor, frame.buffer_id);
      destroy_dumb.handle = frame.buffer_handle;
      drmIoctl(file_descriptor, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
      spdlog::error("unable to map frame buffer");
      return false;
    }

    frame.mapped_memory =
        mmap(nullptr, frame.buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED,
             file_descriptor, __off_t(map_dumb.offset));
    if (frame.mapped_memory == MAP_FAILED) {
      drmModeRmFB(file_descriptor, frame.buffer_id);
      destroy_dumb.handle = frame.buffer_handle;
      drmIoctl(file_descriptor, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
      frame.mapped_memory = nullptr;
      spdlog::error("Unable to map memory using mmap");
      return false;
    }

    memset(frame.mapped_memory, 0, frame.buffer_size);
  }
  return true;
}

void page_flip_callback(int const, unsigned int const, unsigned int const,
                        unsigned int const, void *user_data) {
  auto page_flip_data = reinterpret_cast<page_flip_drm_t *>(user_data);
  if (page_flip_data && page_flip_data->parent_resume)
    page_flip_data->parent_resume();
}
} // namespace details

page_flip_drm_t::~page_flip_drm_t() {
  for (auto &buffer : buffers) {
    if (!buffer.mapped_memory)
      continue;
    munmap(buffer.mapped_memory, buffer.buffer_size);
    drmModeRmFB(file_descriptor, buffer.buffer_id);

    drm_mode_destroy_dumb dumb{};
    memset(&dumb, 0, sizeof dumb);
    dumb.handle = buffer.buffer_handle;
    drmIoctl(file_descriptor, DRM_IOCTL_MODE_DESTROY_DUMB, &dumb);
  }

  if (file_descriptor > 0)
    close(file_descriptor);
}

void async_kms_page_flit_handler_t::run() {
  if (m_streamDesc.has_value())
    return;

  m_streamDesc.emplace(m_ioContext, m_pageFlipDrm->file_descriptor);
  m_streamDesc->async_wait(
      wait_type::wait_read,
      [self = shared_from_this()](auto const &ec) { self->on_ready_read(ec); });
  set_next_timer();
}

void async_kms_page_flit_handler_t::on_ready_read(
    boost::system::error_code const ec) {
  if (ec)
    return spdlog::error("Error occurred: {}", ec.message());

  m_pageFlipDrm->parent_resume = [self = shared_from_this()] {
    self->on_page_flip_occurred();
  };
  drmHandleEvent(m_pageFlipDrm->file_descriptor, &m_pageFlipDrm->event_context);
}

void async_kms_page_flit_handler_t::set_next_timer() {
  // this keeps this object alive for as long as possible
  boost::system::error_code error_code{};
  m_timer.cancel(error_code);
  m_timer.expires_from_now(boost::posix_time::minutes(10), error_code);
  m_timer.async_wait([self = shared_from_this()](auto const &ec) {
    if (ec != net::error::operation_aborted)
      self->set_next_timer();
  });
}

void async_kms_page_flit_handler_t::on_page_flip_occurred() {
  {
    // cancel the keep-alive timer
    boost::system::error_code error_code{};
    m_timer.cancel(error_code);
  }

  // let's flip the current buffer on this write
  m_pageFlipDrm->activated_buffer =
      m_pageFlipDrm->activated_buffer == 0 ? 1 : 0;

  auto const ret = drmModePageFlip(
      m_pageFlipDrm->file_descriptor, m_pageFlipDrm->crtc_id,
      m_pageFlipDrm->buffers[m_pageFlipDrm->activated_buffer].buffer_id,
      DRM_MODE_PAGE_FLIP_EVENT, m_pageFlipDrm);

  if (ret)
    return reset();

  m_streamDesc->async_wait(
      wait_type::wait_read,
      [self = shared_from_this()](auto const &ec) { self->on_ready_read(ec); });
  set_next_timer();
}

void async_kms_page_flit_handler_t::reset() {
  {
    // cancel the keep-alive timer
    boost::system::error_code error_code{};
    m_timer.cancel(error_code);
  }

  m_streamDesc.reset();
  delete m_pageFlipDrm;
}
} // namespace qadx
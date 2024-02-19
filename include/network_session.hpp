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

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

#include "arguments.hpp"
#include "endpoint.hpp"
#include "field_allocs.hpp"
#include <backends/input.hpp>

#define ROUTE_CALLBACK(callback)                                               \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    self->callback(optional_query);                                            \
  }

#define ASYNC_CALLBACK(callback)                                               \
  [self = shared_from_this()](auto const a, auto const b) {                    \
    self->callback(a, b);                                                      \
  }

#define FETCH_EVENT_NUMBER(device)                                             \
  int event_id = 0;                                                            \
  if (event_iter == json_root.end()) {                                         \
    if (!m_rt_arguments.devices.has_value())                                   \
      return error_handler(bad_request("event is not found", request));        \
    event_id = event_id_for(*m_rt_arguments.devices, device);                  \
  } else {                                                                     \
    event_id = event_iter->second.get<json::number_integer_t>();               \
  }

namespace qadx {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using string_response_t = http::response<http::string_body>;
using string_request_t = http::request<http::string_body>;

class session_t : public std::enable_shared_from_this<session_t> {
  using alloc_t = fields_alloc<char>;

private:
  net::io_context &m_ioContext;
  runtime_args_t const &m_rt_arguments;
  endpoint_t m_endpoints;
  beast::flat_buffer m_buffer{};
  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      m_fileResponse = std::nullopt;
  alloc_t m_fileAlloc{8'192};
  std::optional<
      http::response_serializer<http::file_body, http::basic_fields<alloc_t>>>
      m_fileSerializer = std::nullopt;
  std::shared_ptr<void> m_cachedResponse = nullptr;
  std::optional<http::request_parser<http::string_body>> m_clientRequest =
      std::nullopt;
  beast::tcp_stream m_tcpStream;
  string_request_t m_thisRequest{};

private:
  void shutdown_socket();
  void http_read_data();
  void on_data_read(beast::error_code ec, size_t);
  void send_response(string_response_t &&response);
  void error_handler(string_response_t &&response, bool close_socket = false);
  void on_data_written(beast::error_code ec, std::size_t bytes_written);
  void handle_requests(string_request_t const &request);
  void move_mouse_request_handler(url_query_t const &);
  void button_request_handler(url_query_t const &);
  void touch_request_handler(url_query_t const &);
  void swipe_request_handler(url_query_t const &);
  void key_request_handler(url_query_t const &);
  void text_request_handler(url_query_t const &);
  void screen_request_handler(url_query_t const &);
  void screenshot_request_handler(url_query_t const &);
  void send_file(std::filesystem::path const &, string_request_t const &);

public:
  session_t(net::io_context &io, net::ip::tcp::socket &&socket,
            runtime_args_t const &args)
      : m_ioContext(io), m_tcpStream(std::move(socket)), m_rt_arguments(args) {}
  ~session_t();
  std::shared_ptr<session_t> add_endpoint_interfaces();
  void run() { http_read_data(); }
};

} // namespace qadx
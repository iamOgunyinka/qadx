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
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

#include "arguments.hpp"
#include "field_allocs.hpp"
#include <backends/input.hpp>

#define BN_REQUEST_PARAM                                                       \
  (string_request_t const &request, url_query_t const &optional_query)

#define ASYNC_CALLBACK(callback)                                               \
  [self = shared_from_this()](auto const a, auto const b) {                    \
    self->callback(a, b);                                                      \
  }

#define ROUTE_CALLBACK(callback)                                               \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    self->callback(request, optional_query);                                   \
  }

#define JSON_ROUTE_CALLBACK(callback)                                          \
  [self = shared_from_this()] BN_REQUEST_PARAM {                               \
    if (!self->is_json_request())                                              \
      return self->error_handler(                                              \
          bad_request("invalid content-type", request));                       \
    self->callback(request, optional_query);                                   \
  }

namespace qadx {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using string_response_t = http::response<http::string_body>;
using string_request_t = http::request<http::string_body>;
using url_query_t = std::map<boost::string_view, boost::string_view>;
using dynamic_request = http::request_parser<http::string_body>;
using nlohmann::json;

using callback_t =
    std::function<void(string_request_t const &, url_query_t const &)>;

struct rule_t {
  std::vector<http::verb> verbs{};
  callback_t route_callback;

  template <typename Verb, typename... Verbs>
  rule_t(callback_t callback, Verb &&verb, Verbs &&...verbs)
      : verbs{std::forward<Verb>(verb), std::forward<Verbs>(verbs)...},
        route_callback{std::move(callback)} {}
};

class endpoint_t {
  std::map<std::string, rule_t> endpoints;
  using rule_iterator = std::map<std::string, rule_t>::iterator;

public:
  template <typename Verb, typename... Verbs>
  void add_endpoint(std::string const &route, callback_t &&cb, Verb &&verb,
                    Verbs &&...verbs) {
    if (route.empty() || route[0] != '/')
      throw std::runtime_error{"A valid route starts with a /"};
    endpoints.emplace(route, rule_t{std::move(cb), std::forward<Verb>(verb),
                                    std::forward<Verbs>(verbs)...});
  }

  std::optional<rule_iterator> get_rules(std::string const &target);

  std::optional<rule_iterator> get_rules(boost::string_view const &target);
};

class session_t : public std::enable_shared_from_this<session_t> {
  using string_body_ptr =
      std::unique_ptr<http::request_parser<http::string_body>>;
  using alloc_t = fields_alloc<char>;

private:
  net::io_context &m_ioContext;
  runtime_args_t const &m_rt_arguments;
  endpoint_t m_endpoints;
  beast::flat_buffer m_buffer{};
  std::optional<http::request_parser<http::empty_body>> m_emptyBodyParser =
      std::nullopt;
  std::optional<http::response<http::file_body, http::basic_fields<alloc_t>>>
      m_fileResponse = std::nullopt;
  alloc_t m_fileAlloc{8192};
  std::optional<
      http::response_serializer<http::file_body, http::basic_fields<alloc_t>>>
      m_fileSerializer = std::nullopt;
  std::shared_ptr<void> m_cachedResponse = nullptr;
  string_body_ptr m_clientRequest{nullptr};
  beast::tcp_stream m_tcpStream;
  boost::string_view m_contentType{};

private:
  void shutdown_socket();
  void on_header_read(beast::error_code, std::size_t);
  void http_read_data();
  void on_data_read(beast::error_code ec, size_t);
  void send_response(string_response_t &&response);
  void error_handler(string_response_t &&response, bool close_socket = false);
  void on_data_written(beast::error_code ec, std::size_t bytes_written);
  void handle_requests(string_request_t const &request);
  static string_response_t json_success(json const &body,
                                        string_request_t const &req);
  static string_response_t success(char const *message,
                                   string_request_t const &);
  static string_response_t bad_request(std::string const &message,
                                       string_request_t const &);
  static string_response_t not_found(string_request_t const &);
  static string_response_t method_not_allowed(string_request_t const &request);
  static string_response_t server_error(std::string const &,
                                        string_request_t const &);
  static string_response_t get_error(std::string const &, http::status,
                                     string_request_t const &);
  static url_query_t split_optional_queries(boost::string_view const &args);
  bool is_json_request() const;
  void move_mouse_request_handler(string_request_t const &,
                                  url_query_t const &);
  void button_request_handler(string_request_t const &, url_query_t const &);
  void touch_request_handler(string_request_t const &, url_query_t const &);
  void swipe_request_handler(string_request_t const &, url_query_t const &);
  void key_request_handler(string_request_t const &, url_query_t const &);
  void text_request_handler(string_request_t const &, url_query_t const &);
  void screen_request_handler(string_request_t const &, url_query_t const &);
  bool is_closed();

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
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

#include "network_session.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef QADX_USE_ILM
#include "backends/screen/ilm.hpp"
#endif

#include "backends/screen/kms.hpp"
#include "string_utils.hpp"

#ifdef QADX_USE_WITH_WEBSOCKET
#include "websocket_server.hpp"
#include <boost/beast/websocket/rfc6455.hpp>
#endif

#define CONTENT_TYPE_JSON "application/json"

namespace qadx {
using nlohmann::json;

enum constant_e { RequestBodySize = 1'024 * 1'024 };

int event_id_for(input_device_list_t const &device_list,
                 input_device_type_e const type) {
  auto const iter = std::find_if(
      device_list.cbegin(), device_list.cend(),
      [type](auto const &device) { return device.device_type == type; });
  if (iter == device_list.cend())
    return -1;
  return iter->event_number;
}

std::string save_image_to_file(image_data_t const &image) {
  auto const temp_path =
      (std::filesystem::temp_directory_path() / get_random_string(25)).string();
  auto const extension = image.type == image_type_e::png ? "png" : "bmp";
  auto const filename = fmt::format("{}.{}", temp_path, extension);
  std::ofstream out_file(filename, std::ios::out | std::ios::binary);
  if (!out_file)
    return {};
  out_file.write((char *)image.buffer.data(), image.buffer.size());
  out_file.close();
  return filename;
}

// =========================STATIC FUNCTIONS==============================
namespace details {
string_response_t get_error(std::string const &error_message,
                            http::status const status,
                            string_request_t const &req) {
  json::object_t result_obj;
  result_obj["message"] = error_message;
  json result = result_obj;

  using http::field;

  string_response_t response{status, req.version()};
  response.set(http::field::content_type, CONTENT_TYPE_JSON);
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");

  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

string_response_t not_found(string_request_t const &request) {
  return get_error("url not found", http::status::not_found, request);
}

string_response_t server_error(std::string const &message,
                               string_request_t const &request) {
  return get_error(message, http::status::internal_server_error, request);
}

string_response_t bad_request(std::string const &message,
                              string_request_t const &request) {
  return get_error(message, http::status::bad_request, request);
}

string_response_t method_not_allowed(string_request_t const &req) {
  return get_error("method not allowed", http::status::method_not_allowed, req);
}

string_response_t allowed_options(std::vector<http::verb> const &verbs,
                                  string_request_t const &request) {
  std::string buffer{};
  for (size_t i = 0; i < verbs.size() - 1; ++i)
    buffer += std::string(http::to_string(verbs[i])) + ", ";
  buffer += std::string(http::to_string(verbs.back()));

  using http::field;

  string_response_t response{http::status::ok, request.version()};
  response.set(field::allow, buffer);
  response.set(field::cache_control, "max-age=604800");
  response.set(field::server, "qadx-server");
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(http::field::accept_language, "en-us,en;q=0.5");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");
  response.keep_alive(request.keep_alive());
  response.body() = {};
  response.prepare_payload();
  return response;
}

string_response_t json_success(json const &body, string_request_t const &req) {
  using http::field;
  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, CONTENT_TYPE_JSON);
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");

  response.keep_alive(req.keep_alive());
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

string_response_t text_success(std::string const &text,
                               string_request_t const &req) {
  using http::field;
  string_response_t response{http::status::ok, req.version()};
  response.set(http::field::content_type, "text/plain");
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");

  response.keep_alive(req.keep_alive());
  response.body() = text;
  response.prepare_payload();
  return response;
}

string_response_t success(char const *message, string_request_t const &req) {
  json::object_t result_obj;
  result_obj["message"] = message;
  json result(result_obj);

  using http::field;
  string_response_t response{http::status::ok, req.version()};
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");
  response.set(http::field::content_type, CONTENT_TYPE_JSON);
  response.keep_alive(req.keep_alive());
  response.body() = result.dump();
  response.prepare_payload();
  return response;
}

url_query_t split_optional_queries(boost::string_view const &optional_query) {
  url_query_t result{};
  if (!optional_query.empty()) {
    auto queries = utils::split_string_view(optional_query, "&");
    for (auto const &q : queries) {
      auto split = utils::split_string_view(q, "=");
      if (split.size() < 2)
        continue;
      result.emplace(split[0], split[1]);
    }
  }
  return result;
}

} // namespace details

// ==============================

using namespace details;

session_t::~session_t() { spdlog::info("Session completed..."); }

void session_t::http_read_data() {
  m_buffer.clear();
  m_clientRequest.emplace();
  m_clientRequest->body_limit(RequestBodySize);
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::minutes(1));
  http::async_read(m_tcpStream, m_buffer, *m_clientRequest,
                   ASYNC_CALLBACK(on_data_read));
}

void session_t::on_data_read(beast::error_code const ec, std::size_t const) {
  if (ec) {
    if (ec == http::error::end_of_stream) // end of connection
      return shutdown_socket();
    else if (ec == http::error::body_limit) {
      return error_handler(server_error(ec.message(), string_request_t{}),
                           true);
    }
    return error_handler(server_error(ec.message(), string_request_t{}), true);
  }
#ifdef QADX_USE_WITH_WEBSOCKET
  if (beast::websocket::is_upgrade(m_clientRequest->get())) {
    std::make_shared<websocket_server_t>(m_ioContext, m_rt_arguments,
                                         m_tcpStream.release_socket())
        ->run(m_clientRequest->release());
    return;
  }
#endif
  return handle_requests(m_clientRequest->get());
}

void session_t::handle_requests(string_request_t const &request) {
  std::string request_target{utils::decode_url(request.target())};
  while (boost::ends_with(request_target, "/"))
    request_target.pop_back();
  m_thisRequest = request;

  if (request_target.empty())
    return error_handler(details::not_found(request));

  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utils::split_string_view(request_target_view, "?");
  auto const &target = split[0];

  // check the usual rules "table", otherwise check the special routes
  if (auto iter = m_endpoints.get_rules(target); iter.has_value()) {
    if (method == http::verb::options) {
      return send_response(
          allowed_options(iter.value()->second.verbs, request));
    }

    auto const iter_end = iter.value()->second.verbs.cend();
    auto const found_iter =
        std::find(iter.value()->second.verbs.cbegin(), iter_end, method);
    if (found_iter == iter_end)
      return error_handler(method_not_allowed(request));
    boost::string_view const query_string = split.size() > 1 ? split[1] : "";
    auto url_query{split_optional_queries(query_string)};
    return iter.value()->second.route_callback(url_query);
  }

  auto res = m_endpoints.get_special_rules(target);
  if (!res.has_value())
    return error_handler(not_found(request));

  auto &placeholder = *res;
  if (method == http::verb::options)
    return send_response(allowed_options(placeholder.rule->verbs, request));
  auto &rule = placeholder.rule.value();
  auto const found_iter =
      std::find(rule.verbs.cbegin(), rule.verbs.cend(), method);
  if (found_iter == rule.verbs.end())
    return error_handler(method_not_allowed(request));

  boost::string_view const query_string = split.size() > 1 ? split[1] : "";
  auto url_query{split_optional_queries(query_string)};

  for (auto const &[key, value] : placeholder.placeholders)
    url_query[key] = value;
  rule.route_callback(url_query);
}

void session_t::send_response(string_response_t &&response) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  m_cachedResponse = resp;
  http::async_write(m_tcpStream, *resp,
                    beast::bind_front_handler(&session_t::on_data_written,
                                              shared_from_this()));
}

void session_t::shutdown_socket() {
  beast::error_code ec{};
  (void)beast::get_lowest_layer(m_tcpStream)
      .socket()
      .shutdown(net::socket_base::shutdown_send, ec);
  ec = {};
  (void)beast::get_lowest_layer(m_tcpStream).socket().close(ec);
  beast::get_lowest_layer(m_tcpStream).close();
}

void session_t::error_handler(string_response_t &&response, bool close_socket) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  m_cachedResponse = resp;
  if (!close_socket) {
    http::async_write(m_tcpStream, *resp, ASYNC_CALLBACK(on_data_written));
  } else {
    http::async_write(
        m_tcpStream, *resp,
        [self = shared_from_this()](auto const err_c, std::size_t const) {
          self->shutdown_socket();
        });
  }
}

void session_t::on_data_written(beast::error_code ec, std::size_t const) {
  if (ec)
    return spdlog::error(ec.message());

  m_cachedResponse = nullptr;
  http_read_data();
}

std::shared_ptr<session_t> session_t::add_endpoint_interfaces() {
  using http::verb;

  m_endpoints.add_endpoint("/move", ROUTE_CALLBACK(move_mouse_request_handler),
                           verb::post);
  m_endpoints.add_endpoint("/button", ROUTE_CALLBACK(button_request_handler),
                           verb::post);
  m_endpoints.add_endpoint("/touch", ROUTE_CALLBACK(touch_request_handler),
                           verb::post);
  m_endpoints.add_endpoint("/swipe", ROUTE_CALLBACK(swipe_request_handler),
                           verb::post);
  m_endpoints.add_endpoint("/key", ROUTE_CALLBACK(key_request_handler),
                           verb::post);
  m_endpoints.add_endpoint("/text", ROUTE_CALLBACK(text_request_handler),
                           verb::post);
  m_endpoints.add_endpoint("/screen", ROUTE_CALLBACK(screen_request_handler),
                           verb::get);
  m_endpoints.add_special_endpoint("/screen/{screen_number}",
                                   ROUTE_CALLBACK(screenshot_request_handler),
                                   verb::get);
  return shared_from_this();
}

base_screen_t *get_screen_object(runtime_args_t const &args) {
  base_screen_t *screen = nullptr;
  try {
#ifdef QADX_USE_ILM
    if (args.screen_backend == screen_type_e::ilm)
      screen = ilm_screen_t::create_global_instance();
#endif
    if (args.screen_backend == screen_type_e::kms) {
      screen = kms_screen_t::create_global_instance(args.kms_backend_cards,
                                                    args.kms_format_rgb);
    }
  } catch (std::exception const &) {
  }
  return screen;
}

base_input_t *get_input_object(runtime_args_t const &args) {
  base_input_t *base = nullptr;
  if (args.input_backend == input_type_e::evdev)
    base = ev_dev_backend_t::create_global_instance();
  else
    base = uinput_backend_t::create_global_instance();
  return base;
}

void session_t::move_mouse_request_handler(url_query_t const &) {
  auto &request = m_thisRequest;
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto x_iter = json_root.find("x");
    auto y_iter = json_root.find("y");
    auto event_iter = json_root.find("event");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter)) {
      return error_handler(
          bad_request("x/y axis or event is not found", request));
    }

    FETCH_EVENT_NUMBER(input_device_type_e::mouse);

    auto const x = x_iter->second.get<json::number_integer_t>();
    auto const y = y_iter->second.get<json::number_integer_t>();
    auto input_object = get_input_object(m_rt_arguments);
    if (!input_object->move(x, y, event_id))
      error_handler(server_error("Error", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
  send_response(text_success("OK", request));
}

void session_t::button_request_handler(url_query_t const &) {
  auto &request = m_thisRequest;
  try {

    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto event_iter = json_root.find("event");
    auto value_iter = json_root.find("value");

    if (json_root.end() == value_iter)
      return error_handler(bad_request("value is not found", request));

    FETCH_EVENT_NUMBER(input_device_type_e::touchscreen);

    auto const value = value_iter->second.get<json::number_integer_t>();
    auto input_object = get_input_object(m_rt_arguments);
    if (!input_object->button(value, event_id))
      return error_handler(server_error("Error", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
  send_response(text_success("OK", request));
}

void session_t::touch_request_handler(url_query_t const &) {
  auto &request = m_thisRequest;
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto x_iter = json_root.find("x");
    auto y_iter = json_root.find("y");
    auto event_iter = json_root.find("event");
    auto duration_iter = json_root.find("duration");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter,
                                      duration_iter)) {
      return error_handler(
          bad_request("x, y or duration is not found", request));
    }

    FETCH_EVENT_NUMBER(input_device_type_e::touchscreen);
    auto const x = x_iter->second.get<json::number_integer_t>();
    auto const y = y_iter->second.get<json::number_integer_t>();
    auto const duration = duration_iter->second.get<json::number_integer_t>();
    auto input_object = get_input_object(m_rt_arguments);
    if (!input_object->touch(x, y, duration, event_id))
      return error_handler(server_error("Error", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
  send_response(text_success("OK", request));
}

void session_t::key_request_handler(url_query_t const &) {
  auto &request = m_thisRequest;
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto key_iter = json_root.find("key");
    auto event_iter = json_root.find("event");
    if (json_root.end() == key_iter)
      return error_handler(bad_request("event or value is not found", request));

    FETCH_EVENT_NUMBER(input_device_type_e::keyboard);

    auto const key = key_iter->second.get<json::number_integer_t>();
    auto input_object = get_input_object(m_rt_arguments);
    if (!input_object->key(key, event_id))
      return error_handler(server_error("Error", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
  send_response(text_success("OK", request));
}

void session_t::swipe_request_handler(url_query_t const &) {
  auto &request = m_thisRequest;
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto x_iter = json_root.find("x");
    auto x2_iter = json_root.find("x2");
    auto y_iter = json_root.find("y");
    auto y2_iter = json_root.find("y2");
    auto event_iter = json_root.find("event");
    auto velocity_iter = json_root.find("velocity");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter, x2_iter,
                                      y2_iter, velocity_iter)) {
      return error_handler(bad_request(
          "x, y, x2, y2, duration or velocity is not found", request));
    }

    FETCH_EVENT_NUMBER(input_device_type_e::mouse);
    auto const x = x_iter->second.get<json::number_integer_t>();
    auto const x2 = x2_iter->second.get<json::number_integer_t>();
    auto const y = y_iter->second.get<json::number_integer_t>();
    auto const y2 = y2_iter->second.get<json::number_integer_t>();
    auto const velocity = velocity_iter->second.get<json::number_integer_t>();

    auto input_object = get_input_object(m_rt_arguments);
    if (!input_object->swipe(x, y, x2, y2, velocity, event_id))
      return error_handler(server_error("Error", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
  send_response(text_success("OK", request));
}

void session_t::text_request_handler(url_query_t const &) {
  auto &request = m_thisRequest;

  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto text_iter = json_root.find("text");
    auto event_iter = json_root.find("event");

    if (json_root.end() == text_iter)
      return error_handler(bad_request("value is not found", request));

    FETCH_EVENT_NUMBER(input_device_type_e::keyboard);
    auto const text_array = text_iter->second.get<json::array_t>();

    std::vector<int> text_list;
    text_list.reserve(text_array.size());
    for (auto const &text : text_array)
      text_list.push_back(static_cast<int>(text.get<json::number_integer_t>()));

    auto input_object = get_input_object(m_rt_arguments);
    if (!input_object->text(text_list, event_id))
      return error_handler(server_error("Error", request));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
  send_response(text_success("OK", request));
}

void session_t::screenshot_request_handler(url_query_t const &optional_query) {
  spdlog::info("{} called", __func__);
  auto &request = m_thisRequest;
  auto screen_object = get_screen_object(m_rt_arguments);
  if (!screen_object) {
    return error_handler(
        server_error("unable to create screen object.", request));
  }

  auto const id_iter = optional_query.find("screen_number");
  if (id_iter == optional_query.cend())
    return error_handler(bad_request("invalid screen id", request));
  int screen_id;
  try {
    screen_id = std::stoi(id_iter->second);
  } catch (std::exception const &) {
    return error_handler(bad_request("invalid screen id", request));
  }

  image_data_t image{};
  if (!screen_object->grab_frame_buffer(image, screen_id))
    return error_handler(server_error("unable to get screenshot", request));

  auto const filename = save_image_to_file(image);
  send_file(filename, request);
}

void session_t::screen_request_handler(url_query_t const &optional_query) {
  auto screen_object = get_screen_object(m_rt_arguments);
  auto &request = m_thisRequest;

  std::cout << request << std::endl;

  if (!screen_object) {
    return error_handler(
        server_error("unable to create screen object", request));
  }
  return send_response(text_success(screen_object->list_screens(), request));
}

void session_t::send_file(std::filesystem::path const &file_path,
                          string_request_t const &request) {
  std::error_code ec_{};
  if (!std::filesystem::exists(file_path, ec_))
    return error_handler(bad_request("file does not exist", request));

  http::file_body::value_type file;
  beast::error_code ec{};
  file.open(file_path.string().c_str(), beast::file_mode::read, ec);
  if (ec) {
    return error_handler(
        server_error("unable to open file specified", request));
  }

  using http::field;

  auto &response =
      m_fileResponse.emplace(std::piecewise_construct, std::make_tuple(),
                             std::make_tuple(m_fileAlloc));
  response.result(http::status::ok);
  response.keep_alive(request.keep_alive());
  response.set(field::server, "qadx-server");
  response.set(field::access_control_allow_origin, "*");
  response.set(field::access_control_allow_methods, "GET, POST");
  response.set(field::access_control_allow_headers,
               "Content-Type, Authorization");
  response.body() = std::move(file);
  response.prepare_payload();

  m_fileSerializer.emplace(*m_fileResponse);
  http::async_write(m_tcpStream, *m_fileSerializer,
                    [file_path, self = shared_from_this()](
                        beast::error_code const ec, size_t const size_written) {
                      self->m_fileSerializer.reset();
                      self->m_fileResponse.reset();
                      std::filesystem::remove(file_path);
                      self->on_data_written(ec, size_written);
                    });
}
} // namespace qadx
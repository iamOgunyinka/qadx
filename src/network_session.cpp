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
#include <random>
#include <spdlog/spdlog.h>

#include "backends/screen/ilm.hpp"
#include "backends/screen/kms.hpp"
#include "string_utils.hpp"

#define CONTENT_TYPE_JSON "application/json"

namespace qadx {
enum constant_e { RequestBodySize = 1'024 * 1'024 * 50 };

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(std::string const &target) {
  auto iter = m_endpoints.find(target);
  if (iter == m_endpoints.end())
    return std::nullopt;
  return iter;
}

std::optional<endpoint_t::rule_iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(target.to_string());
}

void endpoint_t::construct_special_placeholder(
    endpoint_t::special_placeholders_t &placeholder, std::string const &route) {
  size_t from_pos = 0;
  auto index = route.find_first_of('{', from_pos);
  if (index == std::string::npos || index == 0)
    throw std::runtime_error{"A special route must have a placeholder"};

  std::string const prefix = route.substr(0, index);
  if (auto const str = utils::trim_copy(prefix); str.empty() || str == "/")
    throw std::runtime_error("A special placeholder must have a valid prefix");

  size_t end_of_placeholder;

  do {
    end_of_placeholder = route.find_first_of('}', index);
    if (end_of_placeholder == std::string::npos)
      throw std::runtime_error("end of placeholder not found");
    auto const str_length = end_of_placeholder - index - 1;
    auto const name = utils::trim_copy(route.substr(index + 1, str_length));

    if (name.empty())
      throw std::runtime_error("empty placeholder name is not allowed");

    placeholder.placeholders.push_back({name});
    from_pos = end_of_placeholder + 1;
    if (from_pos >= route.size())
      break;

    index = route.find_first_of('{', from_pos);
    // ensure the character before this is a '/'
    if (index != std::string::npos && route[index - 1] != '/') {
      throw std::runtime_error(
          "special placeholders should be separated by '/'");
    }
  } while (index != std::string ::npos);

  if (auto const str_length = route.length() - end_of_placeholder - 1;
      str_length > 0) {
    placeholder.suffix = route.substr(end_of_placeholder + 1, str_length);
    if (placeholder.suffix.back() == '/')
      placeholder.suffix.pop_back();
  }

  if (m_specialEndpoints.find(prefix) != m_specialEndpoints.end())
    throw std::runtime_error("the prefix '" + prefix + "' already exist");
  m_specialEndpoints[prefix] = std::move(placeholder);
}

std::optional<endpoint_t::special_placeholders_t>
endpoint_t::get_special_rules(std::string target) {
  if (target.back() == '/')
    target.pop_back();

  for (auto const &[prefix, placeholder] : m_specialEndpoints) {
    if (target.find(prefix) != 0)
      continue;
    if (!placeholder.suffix.empty()) {
      if (!boost::ends_with(target, placeholder.suffix))
        continue;
    }

    auto const count = std::count(target.begin(), target.end(), '/');
    if (count != placeholder.placeholders.size())
      continue;
  }

  return std::nullopt;
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

session_t::~session_t() { spdlog::info("Session completed..."); }

void session_t::http_read_data() {
  m_buffer.clear();
  m_emptyBodyParser.emplace();
  m_emptyBodyParser->body_limit(RequestBodySize);
  beast::get_lowest_layer(m_tcpStream).expires_after(std::chrono::minutes(1));
  http::async_read_header(m_tcpStream, m_buffer, *m_emptyBodyParser,
                          ASYNC_CALLBACK(on_header_read));
}

void session_t::on_header_read(beast::error_code ec, std::size_t const) {
  if (ec == http::error::end_of_stream || ec == beast::error::timeout)
    return shutdown_socket();

  if (ec) {
    return error_handler(server_error(ec.message(), {}), true);
  } else {
    m_contentType = m_emptyBodyParser->get()[http::field::content_type];
    m_clientRequest = std::make_unique<http::request_parser<http::string_body>>(
        std::move(*m_emptyBodyParser));
    http::async_read(m_tcpStream, m_buffer, *m_clientRequest,
                     ASYNC_CALLBACK(on_data_read));
  }
}

void session_t::handle_requests(string_request_t const &request) {
  std::string const request_target{utils::decode_url(request.target())};
  if (request_target.empty())
    return error_handler(not_found(request));

  auto const method = request.method();
  boost::string_view request_target_view = request_target;
  auto split = utils::split_string_view(request_target_view, "?");
  if (auto iter = m_endpoints.get_rules(split[0]); iter.has_value()) {
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
    auto url_query_{split_optional_queries(query_string)};
    return iter.value()->second.route_callback(request, url_query_);
  }
  return error_handler(not_found(request));
}

url_query_t
session_t::split_optional_queries(boost::string_view const &optional_query) {
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

  return handle_requests(m_clientRequest->get());
}

void session_t::send_response(string_response_t &&response) {
  auto resp = std::make_shared<string_response_t>(std::move(response));
  m_cachedResponse = resp;
  http::async_write(m_tcpStream, *resp,
                    beast::bind_front_handler(&session_t::on_data_written,
                                              shared_from_this()));
}

bool session_t::is_closed() {
  return !beast::get_lowest_layer(m_tcpStream).socket().is_open();
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

bool session_t::is_json_request() const {
  return boost::iequals(m_contentType, CONTENT_TYPE_JSON);
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

void session_t::on_data_written(
    beast::error_code ec, [[maybe_unused]] std::size_t const bytes_written) {
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
    if (args.screen_backend == screen_type_e::ilm) {
      screen = ilm_screen_t::create_global_instance();
    } else {
      screen = kms_screen_t::create_global_instance(args.kms_backend_cards,
                                                    args.kms_format_rgb);
    }
  } catch (std::exception const &) {
  }
  return screen;
}

input_variant_t get_input_object(runtime_args_t const &args) {
  if (args.input_backend == input_type_e::evdev)
    return ev_dev_backend_t();
  else
    return uinput_backend_t();
}

void session_t::move_mouse_request_handler(string_request_t const &request,
                                           url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto x_iter = json_root.find("x");
    auto y_iter = json_root.find("y");
    auto event_iter = json_root.find("event");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter, event_iter)) {
      return error_handler(
          bad_request("x/y axis or event is not found", request));
    }
    auto const x = x_iter->second.get<json::number_integer_t>();
    auto const y = y_iter->second.get<json::number_integer_t>();
    auto const event = event_iter->second.get<json::number_integer_t>();
    std::visit(
        [x, y, event, request, self = shared_from_this()](auto &&input_object) {
          if (input_object.move(x, y, event))
            return self->send_response(self->json_success("OK", request));
          self->error_handler(self->server_error("Error", request));
        },
        get_input_object(m_rt_arguments));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
}

void session_t::button_request_handler(string_request_t const &request,
                                       url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto event_iter = json_root.find("event");
    auto value_iter = json_root.find("value");
    if (utils::any_element_is_invalid(json_root, value_iter, event_iter)) {
      return error_handler(bad_request("event or value is not found", request));
    }
    auto const event = event_iter->second.get<json::number_integer_t>();
    auto const value = value_iter->second.get<json::number_integer_t>();
    std::visit(
        [value, event, request,
         self = shared_from_this()](auto &&input_object) {
          if (input_object.button(value, event))
            return self->send_response(self->json_success("OK", request));
          self->error_handler(self->server_error("Error", request));
        },
        get_input_object(m_rt_arguments));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
}

void session_t::touch_request_handler(string_request_t const &request,
                                      url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto x_iter = json_root.find("x");
    auto y_iter = json_root.find("y");
    auto event_iter = json_root.find("event");
    auto duration_iter = json_root.find("duration");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter, event_iter,
                                      duration_iter)) {
      return error_handler(
          bad_request("x, y, duration or event is not found", request));
    }
    auto const x = x_iter->second.get<json::number_integer_t>();
    auto const y = y_iter->second.get<json::number_integer_t>();
    auto const event = event_iter->second.get<json::number_integer_t>();
    auto const duration = duration_iter->second.get<json::number_integer_t>();
    spdlog::info("X: {}, Y: {}, event: {}, duration: {}", x, y, event,
                 duration);
    std::visit(
        [x, y, event, duration, request,
         self = shared_from_this()](auto &&input_object) {
          if (input_object.touch(x, y, duration, event))
            return self->send_response(self->json_success("OK", request));
          spdlog::info("Handling error from calling ::touch");
          self->error_handler(self->server_error("Error", request));
        },
        get_input_object(m_rt_arguments));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
}

void session_t::key_request_handler(string_request_t const &request,
                                    url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto key_iter = json_root.find("key");
    auto event_iter = json_root.find("event");
    if (utils::any_element_is_invalid(json_root, key_iter, event_iter)) {
      return error_handler(bad_request("event or value is not found", request));
    }
    auto const event = event_iter->second.get<json::number_integer_t>();
    auto const key = key_iter->second.get<json::number_integer_t>();
    std::visit(
        [key, event, request, self = shared_from_this()](auto &&input_object) {
          if (input_object.key(key, event))
            return self->send_response(self->json_success("OK", request));
          self->error_handler(self->server_error("Error", request));
        },
        get_input_object(m_rt_arguments));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
}

void session_t::swipe_request_handler(string_request_t const &request,
                                      url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto x_iter = json_root.find("x");
    auto x2_iter = json_root.find("x2");
    auto y_iter = json_root.find("y");
    auto y2_iter = json_root.find("y2");
    auto event_iter = json_root.find("event");
    auto velocity_iter = json_root.find("velocity");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter, x2_iter,
                                      y2_iter, event_iter, velocity_iter)) {
      return error_handler(bad_request(
          "x, y, x2, y2, duration or velocity is not found", request));
    }
    auto const x = x_iter->second.get<json::number_integer_t>();
    auto const x2 = x2_iter->second.get<json::number_integer_t>();
    auto const y = y_iter->second.get<json::number_integer_t>();
    auto const y2 = y2_iter->second.get<json::number_integer_t>();
    auto const event = event_iter->second.get<json::number_integer_t>();
    auto const velocity = velocity_iter->second.get<json::number_integer_t>();
    std::visit(
        [x, x2, y, y2, event, velocity, request,
         self = shared_from_this()](auto &&input_object) {
          if (input_object.swipe(x, y, x2, y2, velocity, event))
            return self->send_response(self->json_success("OK", request));
          self->error_handler(self->server_error("Error", request));
        },
        get_input_object(m_rt_arguments));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
}

void session_t::text_request_handler(string_request_t const &request,
                                     url_query_t const &) {
  try {
    auto const json_root = json::parse(request.body()).get<json::object_t>();
    auto text_iter = json_root.find("text");
    auto event_iter = json_root.find("event");
    if (utils::any_element_is_invalid(json_root, text_iter, event_iter)) {
      return error_handler(bad_request("event or value is not found", request));
    }
    auto const event = event_iter->second.get<json::number_integer_t>();
    auto const text_array = text_iter->second.get<json::array_t>();

    std::vector<int> text_list;
    text_list.reserve(text_array.size());
    for (auto const &text : text_array)
      text_list.push_back(static_cast<int>(text.get<json::number_integer_t>()));

    std::visit(
        [list = std::move(text_list), event, request,
         self = shared_from_this()](auto &&input_object) {
          if (input_object.text(list, event))
            return self->send_response(self->json_success("OK", request));
          self->error_handler(self->server_error("Error", request));
        },
        get_input_object(m_rt_arguments));
  } catch (std::exception const &e) {
    spdlog::error(e.what());
    return error_handler(bad_request(e.what(), request));
  }
}

void session_t::screen_request_handler(string_request_t const &request,
                                       url_query_t const &optional_query) {
  auto screen_object = get_screen_object(m_rt_arguments);
  if (!screen_object) {
    return error_handler(
        server_error("unable to create screen object", request));
  }

  auto const id_iter = optional_query.find("id");
  if (id_iter == optional_query.cend())
    return send_response(json_success(screen_object->list_screens(), request));

  // as in the case of /screen?id=xy
  auto const screen_id = id_iter->second.to_string();
  if (screen_id.empty())
    return error_handler(bad_request("invalid screen id", request));

  image_data_t image{};
  if (!screen_object->grab_frame_buffer(image, std::stoi(screen_id)))
    return send_response(server_error("unable to get screenshot", request));

  auto const filename = save_image_to_file(image);
  send_file(filename, request);
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

// =========================STATIC FUNCTIONS==============================

string_response_t session_t::not_found(string_request_t const &request) {
  return get_error("url not found", http::status::not_found, request);
}

string_response_t session_t::server_error(std::string const &message,
                                          string_request_t const &request) {
  return get_error(message, http::status::internal_server_error, request);
}

string_response_t session_t::bad_request(std::string const &message,
                                         string_request_t const &request) {
  return get_error(message, http::status::bad_request, request);
}

string_response_t session_t::method_not_allowed(string_request_t const &req) {
  return get_error("method not allowed", http::status::method_not_allowed, req);
}

string_response_t
session_t::allowed_options(std::vector<http::verb> const &verbs,
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

string_response_t session_t::get_error(std::string const &error_message,
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

string_response_t session_t::json_success(json const &body,
                                          string_request_t const &req) {
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

string_response_t session_t::success(char const *message,
                                     string_request_t const &req) {
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
} // namespace qadx
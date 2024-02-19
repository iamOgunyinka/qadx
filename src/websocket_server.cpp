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

#include "websocket_server.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <nlohmann/json.hpp>
#include <queue>
#include <spdlog/spdlog.h>

#include "backends/screen/ilm.hpp"
#include "backends/screen/kms.hpp"
#include "string_utils.hpp"

#define WEBSOCKET_FETCH_EVENT_NUMBER(device)                                   \
  int event_id = 0;                                                            \
  if (event_iter == json_root.end()) {                                         \
    if (!m_runtimeArgs.devices.has_value())                                    \
      return queue_error_message("event is not found");                        \
    event_id = event_id_for(*m_runtimeArgs.devices, device);                   \
  } else {                                                                     \
    event_id = event_iter->second.get<json::number_integer_t>();               \
  }

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;

namespace qadx {
using nlohmann::json;

base_input_t *get_input_object(runtime_args_t const &args);
base_screen_t *get_screen_object(runtime_args_t const &args);

message_type_e string_to_message(std::string const &str) {
  if (str == "swipe")
    return message_type_e::swipe;
  else if (str == "stream")
    return message_type_e::screen_stream;
  else if (str == "screens")
    return message_type_e::screens;
  else if (str == "text")
    return message_type_e::text;
  else if (str == "key")
    return message_type_e::key;
  else if (str == "touch")
    return message_type_e::touch;
  else if (str == "button")
    return message_type_e::button;
  else
    return message_type_e::unknown;
}

class websocket_server_t::websocket_server_impl_t
    : public std::enable_shared_from_this<
          websocket_server_t::websocket_server_impl_t> {
  net::io_context &m_ioContext;
  runtime_args_t const m_runtimeArgs;
  websocket::stream<beast::tcp_stream> m_webStream;
  std::optional<beast::flat_buffer> m_buffer = std::nullopt;
  std::queue<std::string> m_messageQueue{};

public:
  websocket_server_impl_t(net::io_context &ioContext, runtime_args_t args,
                          ip::tcp::socket &&socket)
      : m_ioContext(ioContext), m_runtimeArgs(std::move(args)),
        m_webStream(std::move(socket)) {}

  void run(string_request_t &&request) {
    spdlog::info("{} called with requested", __func__);
    m_webStream.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));
    m_webStream.set_option(websocket::stream_base::decorator(
        [](websocket::response_type &response) {
          response.set(http::field::server, "qadx-server");
        }));
    m_webStream.async_accept(
        request, [self = shared_from_this()](beast::error_code const ec) {
          self->on_websocket_accepted(ec);
        });
  }

  void on_websocket_accepted(beast::error_code const ec) {
    if (ec)
      return spdlog::error("{} {}", __func__, ec.message());
    read_data();
  }

  void read_data() {
    m_buffer.emplace();

    beast::get_lowest_layer(m_webStream)
        .expires_after(std::chrono::milliseconds(200));
    m_webStream.async_read(
        *m_buffer,
        [self = shared_from_this()](beast::error_code const ec, size_t const) {
          self->on_data_read(ec);
        });
  }

  void on_data_read(beast::error_code const ec) {
    if (ec == beast::error::timeout)
      return send_next_queued_message();

    if (ec == websocket::error::closed)
      return spdlog::info("Websocket connection closed");

    if (ec)
      return spdlog::error("read_data::lambda -> {}", ec.message());

    if (!m_webStream.got_text()) {
      return queue_error_message(
          "unacceptable data type sent, only text expected");
    }

    std::string_view const view((char *)m_buffer->data().data(),
                                m_buffer->size());
    interpret_message(view);
  }

  inline void queue_error_message(std::string const &error_message) {
    m_messageQueue.push(generate_error_message(error_message));
    send_next_queued_message();
  }

  inline void queue_success_message(std::string const &message = "OK") {
    m_messageQueue.push(fmt::format("{\"status\": \"{}\"}", message));
    send_next_queued_message();
  }

  void send_next_queued_message() {
    if (m_messageQueue.empty())
      return read_data();
    write_to_wire();
  }

  void interpret_message(std::string_view const &view) {
    json::object_t json_root;
    message_type_e type;

    try {
      json_root = json::parse(view).get<json::object_t>();
      auto const type_iter = json_root.find("type");
      if (type_iter == json_root.end() || !type_iter->second.is_string())
        return queue_error_message("invalid type");
      type = string_to_message(
          utils::to_lower_copy(type_iter->second.get<json::string_t>()));
    } catch (std::exception const &e) {
      spdlog::error("{} {}", __func__, e.what());
      return queue_error_message(e.what());
    }

    switch (type) {
    case message_type_e::unknown:
      return error_type_sent(view);
    case message_type_e::button:
      return process_button_message(json_root);
    case message_type_e::touch:
      return process_touch_message(json_root);
    case message_type_e::key:
      return process_key_message(json_root);
    case message_type_e::text:
      return process_text_message(json_root);
    case message_type_e::screens:
      return process_list_screens_message(json_root);
    case message_type_e::screen_stream:
      return process_screen_stream_message(json_root);
    case message_type_e::swipe:
      return process_swipe_message(json_root);
    }
  }

  void process_button_message(json::object_t const &json_root) {
    auto event_iter = json_root.find("event");
    auto value_iter = json_root.find("value");

    if (json_root.end() == value_iter)
      return queue_error_message("event or value not found");

    try {
      WEBSOCKET_FETCH_EVENT_NUMBER(input_device_type_e::touchscreen);

      auto const value = value_iter->second.get<json::number_integer_t>();
      auto input_object = get_input_object(m_runtimeArgs);
      if (!input_object->button(value, event_id))
        return queue_error_message("unable to perform button op");
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return queue_error_message(e.what());
    }
    queue_success_message();
  }

  void process_touch_message(json::object_t const &json_root) {

    auto x_iter = json_root.find("x");
    auto y_iter = json_root.find("y");
    auto event_iter = json_root.find("event");
    auto duration_iter = json_root.find("duration");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter,
                                      duration_iter)) {
      return queue_error_message("x, y or duration is not found");
    }

    try {
      WEBSOCKET_FETCH_EVENT_NUMBER(input_device_type_e::touchscreen);

      auto const x = x_iter->second.get<json::number_integer_t>();
      auto const y = y_iter->second.get<json::number_integer_t>();
      auto const duration = duration_iter->second.get<json::number_integer_t>();
      auto input_object = get_input_object(m_runtimeArgs);
      if (!input_object->touch(x, y, duration, event_id))
        return queue_error_message("unable to perform touch op");
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return queue_error_message(e.what());
    }
    queue_success_message();
  }

  void process_key_message(json::object_t const &json_root) {
    auto key_iter = json_root.find("key");
    auto event_iter = json_root.find("event");
    if (json_root.end() == key_iter)
      return queue_error_message("event or value is not found");

    try {
      WEBSOCKET_FETCH_EVENT_NUMBER(input_device_type_e::keyboard);
      auto const key = key_iter->second.get<json::number_integer_t>();
      auto input_object = get_input_object(m_runtimeArgs);
      if (!input_object->key(key, event_id))
        return queue_error_message("unable to perform key event");
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return queue_error_message(e.what());
    }
    queue_success_message();
  }

  void process_text_message(json::object_t const &json_root) {
    auto text_iter = json_root.find("text");
    auto event_iter = json_root.find("event");

    if (json_root.end() == text_iter)
      return queue_error_message("value is not found");

    try {
      WEBSOCKET_FETCH_EVENT_NUMBER(input_device_type_e::keyboard);
      auto const text_array = text_iter->second.get<json::array_t>();

      std::vector<int> text_list;
      text_list.reserve(text_array.size());
      for (auto const &text : text_array) {
        text_list.push_back(
            static_cast<int>(text.get<json::number_integer_t>()));
      }

      auto input_object = get_input_object(m_runtimeArgs);
      if (!input_object->text(text_list, event_id))
        return queue_error_message("unable to perform text op");
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return queue_error_message(e.what());
    }
    queue_success_message();
  }

  void process_list_screens_message(json::object_t const &) {
    auto screen_object = get_screen_object(m_runtimeArgs);

    if (!screen_object)
      return queue_error_message("unable to create screen object");
    queue_success_message(screen_object->list_screens());
  }

  void process_screen_stream_message(json::object_t const &json_root) {
    //
  }

  void process_swipe_message(json::object_t const &json_root) {
    auto x_iter = json_root.find("x");
    auto x2_iter = json_root.find("x2");
    auto y_iter = json_root.find("y");
    auto y2_iter = json_root.find("y2");
    auto event_iter = json_root.find("event");
    auto velocity_iter = json_root.find("velocity");
    if (utils::any_element_is_invalid(json_root, x_iter, y_iter, x2_iter,
                                      y2_iter, velocity_iter)) {
      return queue_error_message(
          "x, y, x2, y2, duration or velocity is not found");
    }

    try {
      WEBSOCKET_FETCH_EVENT_NUMBER(input_device_type_e::mouse);
      auto const x = x_iter->second.get<json::number_integer_t>();
      auto const x2 = x2_iter->second.get<json::number_integer_t>();
      auto const y = y_iter->second.get<json::number_integer_t>();
      auto const y2 = y2_iter->second.get<json::number_integer_t>();
      auto const velocity = velocity_iter->second.get<json::number_integer_t>();

      auto input_object = get_input_object(m_runtimeArgs);
      if (!input_object->swipe(x, y, x2, y2, velocity, event_id))
        return queue_error_message("unable to perform swipe op");
    } catch (std::exception const &e) {
      spdlog::error(e.what());
      return queue_error_message(e.what());
    }
    queue_success_message();
  }

  void write_to_wire() {
    auto &message = m_messageQueue.front();
    m_webStream.async_write(
        net::buffer(message),
        [self = shared_from_this()](beast::error_code const ec, size_t const) {
          assert(!self->m_messageQueue.empty());
          self->m_messageQueue.pop();

          if (ec)
            return spdlog::error("{} {}", __func__, ec.message());
          self->read_data();
        });
  }

  static std::string generate_error_message(std::string const &msg) {
    json::object_t root;
    root["status"] = "error";
    root["message"] = msg;
    return json(root).dump();
  }

  void error_type_sent(std::string_view const &view) {
    json::object_t root;
    root["request"] = view;
    root["status"] = "error";
    root["message"] = "unrecognized type in the message sent";
    // not really a success message, but we just need it queued
    queue_success_message(json(root).dump());
  }
};

websocket_server_t::websocket_server_t(net::io_context &ioContext,
                                       runtime_args_t rt_args,
                                       ip::tcp::socket &&s)
    : m_impl(std::make_shared<websocket_server_impl_t>(
          ioContext, std::move(rt_args), std::move(s))) {}

void websocket_server_t::run(string_request_t &&request) {
  m_impl->run(std::move(request));
}

} // namespace qadx
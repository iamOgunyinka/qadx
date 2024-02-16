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

#include "server.hpp"
#include "network_session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <spdlog/spdlog.h>

namespace qadx {
server_t::server_t(net::io_context &context, runtime_args_t &&args_)
    : m_ioContext(context), m_acceptor(net::make_strand(m_ioContext)),
      m_args(std::move(args_)) {
  beast::error_code ec{}; // used when we don't need to throw all around
  auto const ip_address = "0.0.0.0";
  spdlog::info("Server running on {}:{}", ip_address, m_args.port);
  spdlog::info("Using '{}' for input devices",
               m_args.input_backend == input_type_e::uinput ? "uinput"
                                                            : "evdev");
  spdlog::info("Using '{}' for screen devices",
               m_args.screen_backend == screen_type_e::kms ? "kms" : "ilm");

  tcp::endpoint endpoint(net::ip::make_address(ip_address), m_args.port);
  ec = m_acceptor.open(endpoint.protocol(), ec);
  if (ec) {
    spdlog::error("Could not open socket: {}", ec.message());
    return;
  }

  ec = m_acceptor.set_option(net::socket_base::reuse_address(true), ec);
  if (ec) {
    spdlog::error("set_option failed: {}", ec.message());
    return;
  }

  ec = m_acceptor.bind(endpoint, ec);
  if (ec) {
    spdlog::error("binding failed: {}", ec.message());
    return;
  }

  ec = m_acceptor.listen(net::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("not able to listen: {}", ec.message());
    return;
  }

  m_isOpen = true;
}

bool server_t::run() {
  if (m_isOpen)
    accept_connections();
  return m_isOpen;
}

void server_t::on_connection_accepted(beast::error_code const ec,
                                      tcp::socket socket) {
  if (ec)
    return spdlog::error("error on connection: {}", ec.message());

  std::make_shared<session_t>(m_ioContext, std::move(socket), m_args)
      ->add_endpoint_interfaces()
      ->run();
  accept_connections();
}

void server_t::accept_connections() {
  m_acceptor.async_accept(
      net::make_strand(m_ioContext),
      [self = shared_from_this()](beast::error_code const ec,
                                  tcp::socket socket) {
        return self->on_connection_accepted(ec, std::move(socket));
      });
}

net::io_context &get_io_context() {
  static net::io_context ioContext{
      static_cast<int>(std::thread::hardware_concurrency())};
  return ioContext;
}
} // namespace qadx
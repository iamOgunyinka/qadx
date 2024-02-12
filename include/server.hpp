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

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/error.hpp>
#include <memory>

#include "arguments.hpp"

#define QAD_VERSION "0.0.1"

namespace qad {
namespace net = boost::asio;
namespace beast = boost::beast;

class server_t : public std::enable_shared_from_this<server_t> {
  using tcp = net::ip::tcp;
  net::io_context &m_ioContext;
  tcp::acceptor m_acceptor;
  runtime_args_t const m_args;
  bool m_isOpen = false;

public:
  server_t(net::io_context &context, runtime_args_t &&args);
  explicit operator bool() const { return m_isOpen; }
  bool run();

private:
  void on_connection_accepted(beast::error_code ec, tcp::socket socket);
  void accept_connections();
};

net::io_context &get_io_context();

} // namespace qad

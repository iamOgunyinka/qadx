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
#include "string_utils.hpp"
#include <CLI/CLI11.hpp>
#include <iostream>

namespace qad {
runtime_args_t create_backend_runtime_args(cli_args_t &&cli_args) {
  utils::to_lower_string(cli_args.input_type);
  utils::to_lower_string(cli_args.kms_backend_card);
  utils::to_lower_string(cli_args.screen_backend);

  // expect screen_backend to be in ["kms", "ilm"]
  if (!utils::expect_any_of(cli_args.screen_backend, "kms", "ilm"))
    throw std::runtime_error("invalid screen backend selected");

  // expects input_type to be any of ["uinput", "evdev"]
  if (!utils::expect_any_of(cli_args.input_type, "uinput", "evdev"))
    throw std::runtime_error("invalid input type given");

  runtime_args_t args{};
  if (cli_args.input_type == "uinput")
    args.input_backend = input_type_e::uinput;
  else
    args.input_backend = input_type_e::evdev;

  if (cli_args.screen_backend == "kms") {
    args.screen_backend = screen_type_e::kms;
    args.kms_backend_card = cli_args.kms_backend_card;
    args.kms_format_rgb = cli_args.kms_format_rgb;
  } else {
    args.screen_backend = screen_type_e::ilm;
  }
  args.port = cli_args.port;
  return args;
}
} // namespace qad

int main(int argc, char **argv) {
  CLI::App cli_parser{
      "qad is a simple, REST-API compliant daemon which "
      "makes automated testing on hardware possible by removing the "
      "need for physical intervention as Q.A.D allows inputs to be "
      "injected via http requests",
      "qad"};
  qad::cli_args_t args{};
  cli_parser.add_option("-p,--port", args.port, "port to bind server to");
  cli_parser.add_option("-i,--input-type", args.input_type,
                        "uinput or evdev; defaults to uinput");
  cli_parser.add_option("-s,--screen-backend", args.screen_backend,
                        "kms or ilm; defaults to kms");
  cli_parser.add_option("-k,--kms-backend-card", args.kms_backend_card,
                        "set DRM device; defaults to 'card0'");
  cli_parser.add_flag("-r,--kms-format-rgb", args.kms_format_rgb,
                      "use RGB pixel format instead of BGR");
  cli_parser.set_version_flag("-v,--version", QAD_VERSION);
  CLI11_PARSE(cli_parser, argc, argv)

  [[maybe_unused]] auto rt_args =
      qad::create_backend_runtime_args(std::move(args));
  auto &io_context = qad::get_io_context();
  auto server_instance =
      std::make_shared<qad::server_t>(io_context, std::move(rt_args));
  if (!(*server_instance))
    return EXIT_FAILURE;
  server_instance->run();

  return 0;
}

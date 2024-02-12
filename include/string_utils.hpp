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

#include <boost/utility/string_view.hpp>

namespace qad::utils {
std::string to_lower_copy(std::string const &str);
std::string to_upper_copy(std::string const &str);
void to_lower_string(std::string &str);
void to_upper_string(std::string &str);
void ltrim(std::string &s);
void rtrim(std::string &s);
void trim(std::string &s);
std::string ltrim_copy(std::string s);
std::string rtrim_copy(std::string s);
std::string trim_copy(std::string const &s);
std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delim);
std::string decode_url(boost::string_view const &encoded_string);

template <typename Arg, typename... T>
inline bool expect_any_of(Arg const &first, T &&...args) {
  return (... || (first == std::forward<T>(args)));
}

template <typename Container, typename... IterList>
bool any_element_is_invalid(Container const &container,
                            IterList &&...iter_list) {
  return (... || (std::cend(container) == iter_list));
}
} // namespace qad::utils

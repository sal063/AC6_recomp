#include <string.h>

#include <algorithm>
#include <locale>

#include <native/string.h>

#define UTF_CPP_CPLUSPLUS 201703L
#include <utf8.h>

namespace utfcpp = utf8;

namespace rex::string {

std::string to_utf8(const std::u16string_view source) {
  return utfcpp::utf16to8(source);
}

std::u16string to_utf16(const std::string_view source) {
  return utfcpp::utf8to16(source);
}

std::string_view trim_left(std::string_view sv, std::string_view chars) {
  auto start = sv.find_first_not_of(chars);
  return start == std::string_view::npos ? std::string_view{} : sv.substr(start);
}

std::string_view trim_right(std::string_view sv, std::string_view chars) {
  auto end = sv.find_last_not_of(chars);
  return end == std::string_view::npos ? std::string_view{} : sv.substr(0, end + 1);
}

std::string_view trim(std::string_view sv, std::string_view chars) {
  return trim_right(trim_left(sv, chars), chars);
}

std::string trim_string(std::string_view sv, std::string_view chars) {
  return std::string(trim(sv, chars));
}

}  // namespace rex::string

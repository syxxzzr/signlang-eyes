#ifndef SIGNLANG_EYES_COMMON_FIXED_STRING_HPP
#define SIGNLANG_EYES_COMMON_FIXED_STRING_HPP

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace signlang::common {

  template <std::size_t Size>
  void copy_fixed_string(const char* source, std::array<char, Size>& dest) {
    dest.fill('\0');
    if constexpr (Size > 0U) {
      if (source != nullptr) {
        const auto source_size = std::strlen(source);
        const auto copy_size = std::min<std::size_t>(source_size, Size - 1U);
        std::copy_n(source, copy_size, dest.data());
      }
    }
  }

  template <std::size_t Size>
  void copy_fixed_string(const std::string& source, std::array<char, Size>& dest) {
    copy_fixed_string(source.c_str(), dest);
  }

  template <std::size_t Size>
  auto fixed_string_to_string(const std::array<char, Size>& value) -> std::string {
    const auto end = std::find(value.begin(), value.end(), '\0');
    return std::string{value.begin(), end};
  }

} // namespace signlang::common

#endif // SIGNLANG_EYES_COMMON_FIXED_STRING_HPP

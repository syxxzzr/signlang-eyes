#include "signlang_result.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace signlang::signlang_det {
  namespace {

    auto steady_timestamp_ns_impl() -> std::uint64_t {
      const auto now = std::chrono::steady_clock::now().time_since_epoch();
      return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }

    void copy_string_impl(const char* source, std::array<char, kMaxGestureNameLength>& dest) {
      dest.fill('\0');
      if (source != nullptr) {
        const auto source_len = std::strlen(source);
        const auto copy_size = std::min<std::size_t>(source_len, dest.size() - 1);
        std::copy_n(source, copy_size, dest.data());
      }
    }

  } // namespace

  auto steady_timestamp_ns() -> std::uint64_t { return steady_timestamp_ns_impl(); }

  void copy_string(const char* source, std::array<char, kMaxGestureNameLength>& dest) {
    copy_string_impl(source, dest);
  }

} // namespace signlang::signlang_det

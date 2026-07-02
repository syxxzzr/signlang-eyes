#include "time.hpp"

#include <chrono>

namespace signlang::common {

  auto steady_timestamp_ns() -> std::uint64_t {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  }

} // namespace signlang::common

#ifndef SIGNLANG_EYES_COMMON_CPU_AFFINITY_HPP
#define SIGNLANG_EYES_COMMON_CPU_AFFINITY_HPP

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#ifdef __linux__
#include <cerrno>
#include <cstring>
#include <sched.h>
#endif

namespace signlang::runtime {

  struct CpuAffinityOptions {
    std::optional<std::uint32_t> cpu_core;
    bool requested = false;
  };

  inline auto parse_cpu_core(std::string_view value) -> std::optional<std::uint32_t> {
    if (value.empty()) {
      return std::nullopt;
    }

    std::uint32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [parse_end, parse_error] = std::from_chars(begin, end, parsed);
    if (parse_error != std::errc{} || parse_end != end) {
      return std::nullopt;
    }

    return parsed;
  }

  inline auto bind_current_thread_to_cpu(std::uint32_t cpu_core) -> std::optional<std::string> {
#ifdef __linux__
    if (cpu_core >= CPU_SETSIZE) {
      return std::string{"CPU core index is outside CPU_SETSIZE"};
    }

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu_core, &cpu_set);

    if (::sched_setaffinity(0, sizeof(cpu_set), &cpu_set) != 0) {
      return std::string{"sched_setaffinity failed: "} + std::strerror(errno);
    }

    return std::nullopt;
#else
    (void)cpu_core;
    return std::string{"CPU affinity is only supported on Linux"};
#endif
  }

} // namespace signlang::runtime

#endif // SIGNLANG_EYES_COMMON_CPU_AFFINITY_HPP

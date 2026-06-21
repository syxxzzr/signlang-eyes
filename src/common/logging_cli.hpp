#ifndef SIGNLANG_EYES_COMMON_LOGGING_CLI_HPP
#define SIGNLANG_EYES_COMMON_LOGGING_CLI_HPP

#include "common/logging.hpp"
#include "cxxopts.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace signlang::logging {

inline auto parse_positive_u64(std::string_view value, const char* option_name) -> std::uint64_t {
  std::uint64_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [parse_end, parse_error] = std::from_chars(begin, end, parsed);
  if (parse_error != std::errc{} || parse_end != end || parsed == 0) {
    throw std::runtime_error(std::string{option_name} + " must be a positive integer");
  }
  return parsed;
}

inline void add_cli_options(cxxopts::Options& options) {
  options.add_options("Logging")
      ("log-file", "Write logs to this rotating file in addition to stdout", cxxopts::value<std::string>())
      ("log-rotate-size", "Rotate log files after this many bytes",
       cxxopts::value<std::string>()->default_value(std::to_string(kDefaultRotateSize)));
}

inline auto parse_cli_options(const cxxopts::ParseResult& parsed_options) -> Options {
  return Options{
      .log_file = parsed_options.count("log-file") == 0
          ? std::nullopt
          : std::optional<std::string>{parsed_options["log-file"].as<std::string>()},
      .rotate_size = parse_positive_u64(parsed_options["log-rotate-size"].as<std::string>(), "--log-rotate-size"),
  };
}

} // namespace signlang::logging

#endif // SIGNLANG_EYES_COMMON_LOGGING_CLI_HPP

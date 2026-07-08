#ifndef SIGNLANG_EYES_COMMON_LOGGING_HPP
#define SIGNLANG_EYES_COMMON_LOGGING_HPP

#include "spdlog/logger.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace signlang::logging {

  inline constexpr auto kDefaultRotateSize = std::uint64_t{1048576};
  inline constexpr auto kDefaultRetainFiles = std::uint64_t{100};

  struct Options {
    std::optional<std::string> log_file;
    std::uint64_t rotate_size = kDefaultRotateSize;
    spdlog::level::level_enum level = spdlog::level::info;
  };

  inline auto level_name(spdlog::level::level_enum level) -> const char* {
    switch (level) {
    case spdlog::level::trace:
      return "trace";
    case spdlog::level::debug:
      return "debug";
    case spdlog::level::info:
      return "info";
    case spdlog::level::warn:
      return "warn";
    case spdlog::level::err:
      return "error";
    case spdlog::level::critical:
      return "critical";
    case spdlog::level::off:
      return "off";
    case spdlog::level::n_levels:
      return "unknown";
    }
    return "unknown";
  }

  inline void initialize(const Options& options = {}, std::uint64_t retain_files = kDefaultRetainFiles,
                         const std::string& module_name = "signlang") {
    using SinkPtr = spdlog::sink_ptr;

    std::vector<SinkPtr> sinks;
    sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());

    if (options.log_file.has_value() && !options.log_file->empty()) {
      const auto log_path = std::filesystem::path{*options.log_file};
      const auto parent_path = log_path.parent_path();
      if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
      }

      const auto max_rotated_files = std::max<std::uint64_t>(retain_files, 1);
      sinks.emplace_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          *options.log_file, static_cast<std::size_t>(options.rotate_size),
          static_cast<std::size_t>(max_rotated_files)));
    }

    auto logger =
        std::make_shared<spdlog::logger>(module_name.empty() ? "signlang" : module_name, sinks.begin(), sinks.end());
    logger->set_level(options.level);
    logger->flush_on(options.level);
    logger->set_pattern("[%Y-%m-%dT%H:%M:%S.%e%z] [%l] [%n] %v");
    spdlog::set_default_logger(std::move(logger));
    spdlog::info("logging initialized level={} log_file={} rotate_size={}", level_name(options.level),
                 options.log_file.value_or("<stdout-only>"), options.rotate_size);
  }

} // namespace signlang::logging

#endif // SIGNLANG_EYES_COMMON_LOGGING_HPP

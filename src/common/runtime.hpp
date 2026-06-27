#ifndef SIGNLANG_EYES_COMMON_RUNTIME_HPP
#define SIGNLANG_EYES_COMMON_RUNTIME_HPP

#include "common/logging.hpp"

#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <unistd.h>

namespace signlang::runtime {

  namespace detail {

    template <typename T>
    concept UsageResult = requires(const T& value) {
      { value.text };
    };

    template <typename T>
    concept RuntimeOptions = requires(const T& value) {
      { value.logging };
    };

  } // namespace detail

  inline volatile std::sig_atomic_t g_shutdown_requested = 0;

  inline void request_shutdown(int /* signal_number */) { g_shutdown_requested = 1; }

  inline void install_shutdown_signal_handlers() {
    g_shutdown_requested = 0;
    std::signal(SIGINT, request_shutdown);
    std::signal(SIGTERM, request_shutdown);
  }

  inline auto shutdown_requested() -> bool { return g_shutdown_requested != 0; }

  inline auto executable_path() -> std::filesystem::path {
    auto buffer = std::string(4096, '\0');
    const auto size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size < 0) {
      throw std::runtime_error("Failed to resolve executable path from /proc/self/exe");
    }

    buffer.resize(static_cast<std::size_t>(size));
    return std::filesystem::path{buffer};
  }

  inline auto runtime_root() -> std::filesystem::path {
    auto executable_dir = executable_path().parent_path();
    if (executable_dir.filename() == "bin") {
      return executable_dir.parent_path();
    }
    return executable_dir;
  }

  inline void enter_runtime_root() { std::filesystem::current_path(runtime_root()); }

  inline auto module_name_from_argv(int argc, char** argv) -> std::string {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr || std::string{argv[0]}.empty()) {
      return "signlang";
    }

    return std::filesystem::path{argv[0]}.filename().string();
  }

  template <typename ParseOptions, typename RunModule>
  auto run_module(int argc, char** argv, ParseOptions&& parse_options, RunModule&& run_module) -> int {
    const auto module_name = module_name_from_argv(argc, argv);
    signlang::logging::initialize({}, signlang::logging::kDefaultRetainFiles, module_name);

    try {
      auto parse_result = std::forward<ParseOptions>(parse_options)(argc, argv);
      enter_runtime_root();
      auto exit_code = 0;

      std::visit(
          [&](const auto& result) {
            using Result = std::remove_cvref_t<decltype(result)>;
            if constexpr (detail::UsageResult<Result>) {
              std::cout << result.text << '\n';
            } else {
              static_assert(detail::RuntimeOptions<Result>, "Runtime options must expose a logging field");
              signlang::logging::initialize(result.logging, signlang::logging::kDefaultRetainFiles, module_name);
              install_shutdown_signal_handlers();
              exit_code = std::forward<RunModule>(run_module)(result);
            }
          },
          parse_result);

      return exit_code;
    } catch (const std::exception& error) {
      spdlog::error("{}", error.what());
      return 1;
    }
  }

} // namespace signlang::runtime

#endif // SIGNLANG_EYES_COMMON_RUNTIME_HPP

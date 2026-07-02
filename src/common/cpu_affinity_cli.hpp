#ifndef SIGNLANG_EYES_COMMON_CPU_AFFINITY_CLI_HPP
#define SIGNLANG_EYES_COMMON_CPU_AFFINITY_CLI_HPP

#include "common/cpu_affinity.hpp"
#include "cxxopts.hpp"

#include <string>

namespace signlang::runtime {

  inline void add_cpu_affinity_cli_options(cxxopts::Options& options) {
    options.add_options("Runtime")("cpu-core",
                                   "Best-effort bind this process to a single CPU core index; invalid or rejected "
                                   "values fall back to the system scheduler",
                                   cxxopts::value<std::string>()->implicit_value(""));
  }

  inline auto parse_cpu_affinity_cli_options(const cxxopts::ParseResult& parsed_options) -> CpuAffinityOptions {
    if (parsed_options.count("cpu-core") == 0) {
      return {};
    }

    return CpuAffinityOptions{parse_cpu_core(parsed_options["cpu-core"].as<std::string>()), true};
  }

} // namespace signlang::runtime

#endif // SIGNLANG_EYES_COMMON_CPU_AFFINITY_CLI_HPP

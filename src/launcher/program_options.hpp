#ifndef SIGNLANG_EYES_LAUNCHER_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_LAUNCHER_PROGRAM_OPTIONS_HPP

#include <string>
#include <variant>

namespace signlang::launcher {

  constexpr auto kDefaultConfigPath = "conf/conf.toml";

  struct ProgramOptions {
    std::string config_path;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::launcher

#endif // SIGNLANG_EYES_LAUNCHER_PROGRAM_OPTIONS_HPP

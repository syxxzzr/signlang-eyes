#include "program_options.hpp"

#include "cxxopts.hpp"

namespace signlang::launcher {

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"launcher",
                             "Launch all SignLang Eyes EdgeAI modules with configuration from a TOML file."};

    options.add_options()("c,config", "Path to TOML configuration file",
                          cxxopts::value<std::string>()->default_value(kDefaultConfigPath))("h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    return ProgramOptions{
        .config_path = parsed_options["config"].as<std::string>(),
    };
  }

} // namespace signlang::launcher

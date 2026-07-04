#ifndef SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_PARSER_HPP
#define SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_PARSER_HPP

#include "position.hpp"

#include <optional>
#include <string>

namespace signlang::telemetry_service {

  class NmeaPositionParser {
  public:
    auto parse_line(const std::string& line) -> std::optional<PositionFix>;

  private:
    std::optional<double> latest_altitude_m_;
    std::optional<std::uint8_t> latest_satellites_;
    std::optional<double> latest_hdop_;
  };

} // namespace signlang::telemetry_service

#endif // SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_PARSER_HPP

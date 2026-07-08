#ifndef SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_PARSER_HPP
#define SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_PARSER_HPP

#include "position.hpp"

#include <optional>
#include <string>

namespace signlang::telemetry_service {

  enum class PositionParseStatus {
    Empty,
    InvalidSentence,
    UnsupportedSentence,
    ParseFailed,
    NoFix,
    InvalidCoordinates,
    ValidFix,
  };

  struct PositionParseResult {
    PositionParseStatus status;
    std::optional<PositionFix> fix;
  };

  class NmeaPositionParser {
  public:
    auto parse_line(const std::string& line) -> PositionParseResult;

  private:
    std::optional<double> latest_altitude_m_;
    std::optional<std::uint8_t> latest_satellites_;
    std::optional<double> latest_hdop_;
  };

} // namespace signlang::telemetry_service

#endif // SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_PARSER_HPP

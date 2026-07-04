#ifndef SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_HPP
#define SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace signlang::telemetry_service {

  struct PositionFix {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    std::optional<double> altitude_m;
    std::optional<double> speed_kph;
    std::optional<double> track_deg;
    std::optional<std::uint8_t> satellites;
    std::optional<double> hdop;
    std::string source_sentence;
  };

} // namespace signlang::telemetry_service

#endif // SIGNLANG_EYES_TELEMETRY_SERVICE_POSITION_HPP

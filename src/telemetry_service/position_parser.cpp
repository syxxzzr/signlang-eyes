#include "position_parser.hpp"

#include "minmea.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace signlang::telemetry_service {
  namespace {

    auto coord_to_degrees(const minmea_float& value) -> std::optional<double> {
      if (value.scale == 0) {
        return std::nullopt;
      }

      const auto raw = minmea_tocoord(&value);
      if (!std::isfinite(raw)) {
        return std::nullopt;
      }
      return raw;
    }

    auto fraction_to_double(const minmea_float& value) -> std::optional<double> {
      if (value.scale == 0) {
        return std::nullopt;
      }
      return minmea_tofloat(&value);
    }

    auto copy_sentence(const std::string& line) -> std::string {
      auto sentence = line;
      sentence.erase(std::remove(sentence.begin(), sentence.end(), '\r'), sentence.end());
      sentence.erase(std::remove(sentence.begin(), sentence.end(), '\n'), sentence.end());
      return sentence;
    }

  } // namespace

  auto NmeaPositionParser::parse_line(const std::string& line) -> PositionParseResult {
    const auto sentence = copy_sentence(line);
    if (sentence.empty()) {
      return PositionParseResult{PositionParseStatus::Empty, std::nullopt};
    }

    if (!minmea_check(sentence.c_str(), false)) {
      return PositionParseResult{PositionParseStatus::InvalidSentence, std::nullopt};
    }

    switch (minmea_sentence_id(sentence.c_str(), false)) {
      case MINMEA_SENTENCE_GGA: {
        minmea_sentence_gga frame{};
        if (!minmea_parse_gga(&frame, sentence.c_str())) {
          return PositionParseResult{PositionParseStatus::ParseFailed, std::nullopt};
        }
        if (frame.fix_quality == 0) {
          return PositionParseResult{PositionParseStatus::NoFix, std::nullopt};
        }

        const auto latitude = coord_to_degrees(frame.latitude);
        const auto longitude = coord_to_degrees(frame.longitude);
        if (!latitude.has_value() || !longitude.has_value()) {
          return PositionParseResult{PositionParseStatus::InvalidCoordinates, std::nullopt};
        }

        latest_altitude_m_ = fraction_to_double(frame.altitude);
        latest_satellites_ = frame.satellites_tracked < 0
            ? std::optional<std::uint8_t>{}
            : std::optional<std::uint8_t>{static_cast<std::uint8_t>(
                  std::min(frame.satellites_tracked, static_cast<int>(std::numeric_limits<std::uint8_t>::max())))};
        latest_hdop_ = fraction_to_double(frame.hdop);

        auto fix = PositionFix{};
        fix.latitude_deg = *latitude;
        fix.longitude_deg = *longitude;
        fix.altitude_m = latest_altitude_m_;
        fix.satellites = latest_satellites_;
        fix.hdop = latest_hdop_;
        fix.source_sentence = sentence;
        return PositionParseResult{PositionParseStatus::ValidFix, std::move(fix)};
      }
      case MINMEA_SENTENCE_RMC: {
        minmea_sentence_rmc frame{};
        if (!minmea_parse_rmc(&frame, sentence.c_str())) {
          return PositionParseResult{PositionParseStatus::ParseFailed, std::nullopt};
        }
        if (!frame.valid) {
          return PositionParseResult{PositionParseStatus::NoFix, std::nullopt};
        }

        const auto latitude = coord_to_degrees(frame.latitude);
        const auto longitude = coord_to_degrees(frame.longitude);
        if (!latitude.has_value() || !longitude.has_value()) {
          return PositionParseResult{PositionParseStatus::InvalidCoordinates, std::nullopt};
        }

        auto speed_kph = fraction_to_double(frame.speed);
        if (speed_kph.has_value()) {
          *speed_kph *= 1.852;
        }

        auto fix = PositionFix{};
        fix.latitude_deg = *latitude;
        fix.longitude_deg = *longitude;
        fix.altitude_m = latest_altitude_m_;
        fix.speed_kph = speed_kph;
        fix.track_deg = fraction_to_double(frame.course);
        fix.satellites = latest_satellites_;
        fix.hdop = latest_hdop_;
        fix.source_sentence = sentence;
        return PositionParseResult{PositionParseStatus::ValidFix, std::move(fix)};
      }
      default:
        return PositionParseResult{PositionParseStatus::UnsupportedSentence, std::nullopt};
    }
  }

} // namespace signlang::telemetry_service

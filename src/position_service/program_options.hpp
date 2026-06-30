#ifndef SIGNLANG_EYES_POSITION_SERVICE_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_POSITION_SERVICE_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::position_service {

  struct ProgramOptions {
    std::string serial_device;
    std::uint32_t baud_rate;
    std::string mqtt_host;
    std::uint16_t mqtt_port;
    std::string mqtt_client_id;
    std::string mqtt_topic;
    std::string alert_event_service;
    std::string alert_mqtt_topic;
    std::string mqtt_username;
    std::string mqtt_password;
    std::uint16_t keep_alive_seconds;
    std::uint8_t qos;
    bool retain;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::position_service

#endif // SIGNLANG_EYES_POSITION_SERVICE_PROGRAM_OPTIONS_HPP

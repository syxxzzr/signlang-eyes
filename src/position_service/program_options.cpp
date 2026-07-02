#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace signlang::position_service {
  namespace {

    constexpr auto kDefaultSerialDevice = "/dev/ttyS9";
    constexpr std::uint32_t kDefaultBaudRate = 115200;
    constexpr auto kDefaultMqttHost = "127.0.0.1";
    constexpr std::uint16_t kDefaultMqttPort = 1883;
    constexpr auto kDefaultMqttClientId = "signlang_eyes_position_service";
    constexpr auto kDefaultMqttTopic = "signlang/position";
    constexpr auto kDefaultAlertEventService = "position_alert";
    constexpr auto kDefaultAlertMqttTopic = "signlang/alert";
    constexpr std::uint16_t kDefaultKeepAliveSeconds = 30;

    auto parse_port(const cxxopts::ParseResult& parsed_options, const char* option_name) -> std::uint16_t {
      const auto value = parsed_options[option_name].as<std::uint32_t>();
      if (value == 0 || value > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error(std::string{"--"} + option_name + " must be in the range [1, 65535]");
      }
      return static_cast<std::uint16_t>(value);
    }

    auto parse_qos(const cxxopts::ParseResult& parsed_options) -> std::uint8_t {
      const auto value = parsed_options["mqtt-qos"].as<std::uint32_t>();
      if (value > 2) {
        throw std::runtime_error("--mqtt-qos must be one of 0, 1, 2");
      }
      return static_cast<std::uint8_t>(value);
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_position_service",
                             "Read GNSS NMEA from a serial device and publish positions to MQTT."};

    options.add_options()("d,device", "GNSS serial device path",
                          cxxopts::value<std::string>()->default_value(kDefaultSerialDevice))(
        "baud-rate", "GNSS serial baud rate",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultBaudRate)))(
        "mqtt-host", "MQTT broker host", cxxopts::value<std::string>()->default_value(kDefaultMqttHost))(
        "mqtt-port", "MQTT broker TCP port",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMqttPort)))(
        "mqtt-client-id", "MQTT client id", cxxopts::value<std::string>()->default_value(kDefaultMqttClientId))(
        "mqtt-topic", "MQTT topic for position JSON payloads",
        cxxopts::value<std::string>()->default_value(kDefaultMqttTopic))(
        "alert-event-service", "iceoryx2 event service name that triggers MQTT alert payloads",
        cxxopts::value<std::string>()->default_value(kDefaultAlertEventService))(
        "alert-mqtt-topic", "MQTT topic for alert JSON payloads",
        cxxopts::value<std::string>()->default_value(kDefaultAlertMqttTopic))(
        "mqtt-username", "MQTT username", cxxopts::value<std::string>()->default_value(""))(
        "mqtt-password", "MQTT password", cxxopts::value<std::string>()->default_value(""))(
        "mqtt-keep-alive", "MQTT keep-alive in seconds",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultKeepAliveSeconds)))(
        "mqtt-qos", "MQTT publish QoS: 0, 1, 2", cxxopts::value<std::uint32_t>()->default_value("0"))(
        "mqtt-retain", "Publish retained MQTT messages", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    const auto baud_rate = parsed_options["baud-rate"].as<std::uint32_t>();
    if (baud_rate == 0) {
      throw std::runtime_error("--baud-rate must be greater than 0");
    }

    const auto keep_alive = parsed_options["mqtt-keep-alive"].as<std::uint32_t>();
    if (keep_alive == 0 || keep_alive > std::numeric_limits<std::uint16_t>::max()) {
      throw std::runtime_error("--mqtt-keep-alive must be in the range [1, 65535]");
    }

    auto mqtt_topic = parsed_options["mqtt-topic"].as<std::string>();
    if (mqtt_topic.empty()) {
      throw std::runtime_error("--mqtt-topic must not be empty");
    }
    auto alert_mqtt_topic = parsed_options["alert-mqtt-topic"].as<std::string>();
    if (alert_mqtt_topic.empty()) {
      throw std::runtime_error("--alert-mqtt-topic must not be empty");
    }

    return ProgramOptionsParseResult{ProgramOptions{parsed_options["device"].as<std::string>(),
                                                    baud_rate,
                                                    parsed_options["mqtt-host"].as<std::string>(),
                                                    parse_port(parsed_options, "mqtt-port"),
                                                    parsed_options["mqtt-client-id"].as<std::string>(),
                                                    std::move(mqtt_topic),
                                                    parsed_options["alert-event-service"].as<std::string>(),
                                                    std::move(alert_mqtt_topic),
                                                    parsed_options["mqtt-username"].as<std::string>(),
                                                    parsed_options["mqtt-password"].as<std::string>(),
                                                    static_cast<std::uint16_t>(keep_alive),
                                                    parse_qos(parsed_options),
                                                    parsed_options["mqtt-retain"].as<bool>(),
                                                    signlang::logging::parse_cli_options(parsed_options)}};
  }

} // namespace signlang::position_service

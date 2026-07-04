#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::peripheral_service {
  namespace {

    constexpr auto kDefaultSerialDevice = "/dev/ttyS3";
    constexpr std::uint32_t kDefaultBaudRate = 115200;
    constexpr auto kDefaultFontFile = "conf/unifont-17.0.05.hex";
    constexpr auto kDefaultDisplayService = "peripheral_display";
    constexpr auto kDefaultStateControlService = "app_state_control";
    constexpr auto kDefaultAlertEventService = "telemetry_alert";

    [[nodiscard]] auto parse_u8(const cxxopts::ParseResult& parsed_options, const char* name, std::uint32_t min,
                                std::uint32_t max) -> std::uint8_t {
      const auto value = parsed_options[name].as<std::uint32_t>();
      if (value < min || value > max || value > std::numeric_limits<std::uint8_t>::max()) {
        throw std::runtime_error(std::string{"--"} + name + " must be in the range [" + std::to_string(min) + ", " +
                                 std::to_string(max) + "]");
      }
      return static_cast<std::uint8_t>(value);
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_peripheral_service",
                             "Drive the OLED, vibration motor, and button peripheral over UART."};

    options.add_options()("d,device", "Peripheral serial device path",
                          cxxopts::value<std::string>()->default_value(kDefaultSerialDevice))(
        "baud-rate", "Peripheral serial baud rate",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultBaudRate)))(
        "data-bits", "Peripheral serial data bits", cxxopts::value<std::uint32_t>()->default_value("8"))(
        "stop-bits", "Peripheral serial stop bits: 1 or 2", cxxopts::value<std::uint32_t>()->default_value("1"))(
        "parity", "Peripheral serial parity: none, odd, even", cxxopts::value<std::string>()->default_value("none"))(
        "flow-control", "Peripheral serial flow control: none, software, hardware",
        cxxopts::value<std::string>()->default_value("none"))(
        "font-file", "GNU Unifont .hex file used for OLED text rendering",
        cxxopts::value<std::string>()->default_value(kDefaultFontFile))(
        "display-width", "OLED width in pixels", cxxopts::value<std::uint32_t>()->default_value("128"))(
        "display-height", "OLED height in pixels", cxxopts::value<std::uint32_t>()->default_value("32"))(
        "font-size", "Rendered font height in pixels", cxxopts::value<std::uint32_t>()->default_value("16"))(
        "char-spacing", "Spacing between rendered glyphs in pixels",
        cxxopts::value<std::uint32_t>()->default_value("1"))(
        "line-gap", "Spacing between the two OLED text lines in pixels",
        cxxopts::value<std::uint32_t>()->default_value("0"))(
        "scroll-step-px", "Horizontal scroll step per scroll tick in pixels",
        cxxopts::value<std::uint32_t>()->default_value("1"))(
        "scroll-interval-ms", "Horizontal scroll interval in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value("80"))(
        "refresh-interval-ms", "OLED refresh worker interval in milliseconds",
        cxxopts::value<std::uint32_t>()->default_value("50"))(
        "display-service", "iceoryx2 request-response service for display text",
        cxxopts::value<std::string>()->default_value(kDefaultDisplayService))(
        "state-control-service", "iceoryx2 app state control request-response service",
        cxxopts::value<std::string>()->default_value(kDefaultStateControlService))(
        "alert-event-service", "iceoryx2 alert event service consumed by telemetry_service",
        cxxopts::value<std::string>()->default_value(kDefaultAlertEventService))("h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    auto serial = SerialOptions{parsed_options["device"].as<std::string>(),
                                parsed_options["baud-rate"].as<std::uint32_t>(),
                                parsed_options["data-bits"].as<std::uint32_t>(),
                                parsed_options["stop-bits"].as<std::uint32_t>(),
                                serial_parity_from_string(parsed_options["parity"].as<std::string>()),
                                serial_flow_control_from_string(parsed_options["flow-control"].as<std::string>())};
    if (serial.device.empty()) {
      throw std::runtime_error("--device must not be empty");
    }
    if (serial.baud_rate == 0) {
      throw std::runtime_error("--baud-rate must be greater than 0");
    }
    if (serial.data_bits < 5 || serial.data_bits > 8) {
      throw std::runtime_error("--data-bits must be in the range [5, 8]");
    }
    if (serial.stop_bits != 1 && serial.stop_bits != 2) {
      throw std::runtime_error("--stop-bits must be 1 or 2");
    }

    auto display = DisplayOptions{parse_u8(parsed_options, "display-width", 1, 128),
                                  parse_u8(parsed_options, "display-height", 8, 64),
                                  parse_u8(parsed_options, "font-size", 1, 32),
                                  parse_u8(parsed_options, "char-spacing", 0, 16),
                                  parse_u8(parsed_options, "line-gap", 0, 16),
                                  parse_u8(parsed_options, "scroll-step-px", 1, 32),
                                  parsed_options["scroll-interval-ms"].as<std::uint32_t>(),
                                  parsed_options["refresh-interval-ms"].as<std::uint32_t>()};
    if ((display.height % 8U) != 0U) {
      throw std::runtime_error("--display-height must be divisible by 8");
    }
    if (display.scroll_interval_ms == 0 || display.refresh_interval_ms == 0) {
      throw std::runtime_error("--scroll-interval-ms and --refresh-interval-ms must be greater than 0");
    }

    auto font_file = parsed_options["font-file"].as<std::string>();
    if (font_file.empty()) {
      throw std::runtime_error("--font-file must not be empty");
    }

    return ProgramOptionsParseResult{ProgramOptions{std::move(serial),
                                                    display,
                                                    std::move(font_file),
                                                    parsed_options["display-service"].as<std::string>(),
                                                    parsed_options["state-control-service"].as<std::string>(),
                                                    parsed_options["alert-event-service"].as<std::string>(),
                                                    signlang::logging::parse_cli_options(parsed_options)}};
  }

} // namespace signlang::peripheral_service

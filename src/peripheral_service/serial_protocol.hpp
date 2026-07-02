#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_SERIAL_PROTOCOL_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_SERIAL_PROTOCOL_HPP

#include <cstdint>
#include <optional>
#include <vector>

namespace signlang::peripheral_service {

  constexpr auto kOledWidth = std::uint8_t{128};
  constexpr auto kOledHeight = std::uint8_t{32};
  constexpr auto kMaxFrameDataLength = std::uint8_t{128};

  enum class OledCommand : std::uint8_t {
    Clear = 0x01,
    Refresh = 0x02,
    DrawBlock = 0x03,
    Motor = 0x07,
  };

  enum class ButtonEvent : std::uint8_t {
    SingleClick = 0xA1,
    DoubleClick = 0xA2,
  };

  struct OledBlock {
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t width;
    std::uint8_t height;
    std::vector<std::uint8_t> data;
  };

  [[nodiscard]] auto make_frame(OledCommand command, std::uint8_t x, std::uint8_t y, std::uint8_t width,
                                std::uint8_t height, const std::vector<std::uint8_t>& data)
      -> std::vector<std::uint8_t>;
  [[nodiscard]] auto make_clear_frame() -> std::vector<std::uint8_t>;
  [[nodiscard]] auto make_refresh_frame() -> std::vector<std::uint8_t>;
  [[nodiscard]] auto make_motor_frame(bool enabled) -> std::vector<std::uint8_t>;
  [[nodiscard]] auto split_draw_block(const OledBlock& block) -> std::vector<std::vector<std::uint8_t>>;
  [[nodiscard]] auto parse_button_event(std::uint8_t value) -> std::optional<ButtonEvent>;

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_SERIAL_PROTOCOL_HPP

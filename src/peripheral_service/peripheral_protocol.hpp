#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_PERIPHERAL_PROTOCOL_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_PERIPHERAL_PROTOCOL_HPP

#include <array>
#include <cstdint>
#include <type_traits>

namespace signlang::peripheral_service {

  constexpr auto kDisplayTextLength = std::uint32_t{256};
  constexpr auto kDisplayMessageLength = std::uint32_t{128};

  enum class DisplayCommand : std::uint32_t {
    SetTitleLine = 0,
    SetContentLine = 1,
    ClearContentLine = 2,
  };

  enum class DisplayStatus : std::uint32_t {
    Ok = 0,
    BadRequest = 1,
    Failed = 2,
  };

  struct DisplayRequest {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_peripheral_display_request";

    std::uint32_t request_id;
    DisplayCommand command;
    std::array<char, kDisplayTextLength> text;
  };

  struct DisplayResponse {
    static constexpr const char* IOX2_TYPE_NAME = "signlang_peripheral_display_response";

    std::uint32_t request_id;
    DisplayStatus status;
    std::array<char, kDisplayMessageLength> message;
  };

  static_assert(std::is_trivially_copyable_v<DisplayRequest>);
  static_assert(std::is_trivially_copyable_v<DisplayResponse>);

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_PERIPHERAL_PROTOCOL_HPP

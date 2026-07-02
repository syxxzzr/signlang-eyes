#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_OLED_FRAMEBUFFER_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_OLED_FRAMEBUFFER_HPP

#include "serial_protocol.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace signlang::peripheral_service {

  struct Rect {
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t width;
    std::uint8_t height;
  };

  class OledFramebuffer {
  public:
    OledFramebuffer(std::uint8_t width = kOledWidth, std::uint8_t height = kOledHeight);

    [[nodiscard]] auto width() const -> std::uint8_t;
    [[nodiscard]] auto height() const -> std::uint8_t;
    [[nodiscard]] auto get(std::uint8_t x, std::uint8_t y) const -> bool;
    void set(std::uint8_t x, std::uint8_t y, bool value);
    void clear();
    void clear_rect(Rect rect);
    [[nodiscard]] auto to_block(Rect rect) const -> OledBlock;
    [[nodiscard]] auto diff_rect(const OledFramebuffer& other) const -> std::optional<Rect>;
    [[nodiscard]] auto page_aligned(Rect rect) const -> Rect;

  private:
    [[nodiscard]] auto index(std::uint8_t x, std::uint8_t y) const -> std::size_t;

    std::uint8_t width_;
    std::uint8_t height_;
    std::vector<std::uint8_t> pixels_;
  };

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_OLED_FRAMEBUFFER_HPP

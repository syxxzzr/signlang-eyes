#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_SCROLLING_DISPLAY_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_SCROLLING_DISPLAY_HPP

#include "hex_font.hpp"
#include "oled_framebuffer.hpp"
#include "text_renderer.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace signlang::peripheral_service {

  struct DisplayOptions {
    std::uint8_t width = kOledWidth;
    std::uint8_t height = kOledHeight;
    std::uint8_t font_size = 16;
    std::uint8_t char_spacing = 1;
    std::uint8_t line_gap = 0;
    std::uint8_t scroll_step_px = 1;
    std::uint32_t scroll_interval_ms = 80;
    std::uint32_t refresh_interval_ms = 50;
  };

  class ScrollingDisplay {
  public:
    using Clock = std::chrono::steady_clock;

    ScrollingDisplay(const HexFont& font, DisplayOptions options);

    void set_first_line(std::string text);
    void set_second_line(std::string text);
    void clear_second_line();
    [[nodiscard]] auto tick(Clock::time_point now) -> std::vector<std::vector<std::uint8_t>>;
    [[nodiscard]] auto full_refresh() -> std::vector<std::vector<std::uint8_t>>;

  private:
    struct LineState {
      std::string text;
      std::uint16_t width = 0;
      std::uint16_t offset = 0;
    };

    void update_widths();
    void advance_scroll(Clock::time_point now);
    void render(OledFramebuffer& framebuffer) const;
    void draw_line(OledFramebuffer& framebuffer, const LineState& line, std::uint8_t y) const;
    [[nodiscard]] auto frames_for_current_dirty_region() -> std::vector<std::vector<std::uint8_t>>;

    const HexFont& font_;
    DisplayOptions options_;
    TextRenderer renderer_;
    OledFramebuffer displayed_;
    LineState first_line_;
    LineState second_line_;
    Clock::time_point last_scroll_at_{};
    Clock::time_point last_refresh_at_{};
    bool force_refresh_{true};
  };

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_SCROLLING_DISPLAY_HPP

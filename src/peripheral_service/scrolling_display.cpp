#include "scrolling_display.hpp"

#include "serial_protocol.hpp"

#include <algorithm>
#include <utility>

namespace signlang::peripheral_service {
  ScrollingDisplay::ScrollingDisplay(const HexFont& font, DisplayOptions options) :
      font_{font},
      options_{options},
      renderer_{font_, TextRenderOptions{options_.font_size, options_.char_spacing}},
      displayed_{options_.width, options_.height} {}

  void ScrollingDisplay::set_first_line(std::string text) {
    if (first_line_.text == text) {
      return;
    }
    first_line_.text = std::move(text);
    first_line_.offset = 0;
    first_line_.pause_until = Clock::time_point{};
    first_line_.scroll_finished = false;
    update_widths();
    force_refresh_ = true;
  }

  void ScrollingDisplay::set_second_line(std::string text) {
    if (second_line_.text == text) {
      return;
    }
    second_line_.text = std::move(text);
    second_line_.offset = 0;
    second_line_.pause_until = Clock::time_point{};
    second_line_.scroll_finished = false;
    update_widths();
    force_refresh_ = true;
  }

  void ScrollingDisplay::clear_second_line() { set_second_line({}); }

  auto ScrollingDisplay::tick(Clock::time_point now) -> std::vector<std::vector<std::uint8_t>> {
    advance_scroll(now);
    if (!force_refresh_) {
      return {};
    }

    const auto frames = frames_for_current_dirty_region();
    force_refresh_ = false;
    return frames;
  }

  void ScrollingDisplay::update_widths() {
    first_line_.width = renderer_.measure_width(first_line_.text);
    second_line_.width = renderer_.measure_width(second_line_.text);
  }

  void ScrollingDisplay::advance_scroll(Clock::time_point now) {
    if (last_scroll_at_ == Clock::time_point{}) {
      last_scroll_at_ = now;
      return;
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_scroll_at_).count();
    if (elapsed_ms <= 0) {
      return;
    }
    last_scroll_at_ = now;

    const auto scroll_units = scroll_remainder_px_ms_ +
                              static_cast<std::uint32_t>(elapsed_ms) * options_.scroll_speed_px_per_sec;
    const auto advance_px = static_cast<std::uint16_t>(scroll_units / 1000U);
    scroll_remainder_px_ms_ = scroll_units % 1000U;
    if (advance_px == 0U) {
      return;
    }

    auto changed = false;
    auto advance_line = [this, now, advance_px, &changed](LineState& line) {
      if (line.width <= options_.width) {
        if (line.offset != 0U) {
          line.offset = 0;
          line.pause_until = Clock::time_point{};
          line.scroll_finished = false;
          changed = true;
        }
        return;
      }
      if (line.scroll_finished) {
        return;
      }

      if (line.pause_until == Clock::time_point{}) {
        line.pause_until = now + std::chrono::milliseconds{options_.scroll_pause_ms};
        return;
      }
      if (now < line.pause_until) {
        return;
      }

      const auto max_offset = static_cast<std::uint16_t>(line.width - options_.width);
      if (line.offset >= max_offset) {
        if (options_.scroll_loop) {
          line.offset = 0;
          line.pause_until = now + std::chrono::milliseconds{options_.scroll_pause_ms};
          changed = true;
        } else if (now >= line.pause_until) {
          line.scroll_finished = true;
        }
        return;
      }

      const auto next_offset = static_cast<std::uint32_t>(line.offset) + advance_px;
      line.offset = static_cast<std::uint16_t>(std::min<std::uint32_t>(next_offset, max_offset));
      if (line.offset >= max_offset) {
        line.pause_until = now + std::chrono::milliseconds{options_.scroll_pause_ms};
      }
      changed = true;
    };

    advance_line(first_line_);
    advance_line(second_line_);
    if (changed) {
      force_refresh_ = true;
      last_scroll_at_ = now;
    }
  }

  void ScrollingDisplay::render(OledFramebuffer& framebuffer) const {
    framebuffer.clear();
    draw_line(framebuffer, first_line_, 0);
    const auto second_line_y = static_cast<std::uint16_t>(options_.font_size) + options_.line_gap;
    if (second_line_y < options_.height) {
      draw_line(framebuffer, second_line_, static_cast<std::uint8_t>(second_line_y));
    }
  }

  void ScrollingDisplay::draw_line(OledFramebuffer& framebuffer, const LineState& line, std::uint8_t y) const {
    if (line.text.empty()) {
      return;
    }

    if (line.width <= options_.width) {
      renderer_.draw_text(framebuffer, line.text, 0, y);
      return;
    }

    renderer_.draw_text(framebuffer, line.text, -static_cast<std::int16_t>(line.offset), y);
  }

  auto ScrollingDisplay::frames_for_current_dirty_region() -> std::vector<std::vector<std::uint8_t>> {
    auto next = OledFramebuffer{options_.width, options_.height};
    render(next);

    auto dirty = displayed_.diff_rect(next);
    if (!dirty.has_value()) {
      return {};
    }

    const auto aligned = next.page_aligned(*dirty);
    auto frames = split_draw_block(next.to_block(aligned));
    frames.push_back(make_refresh_frame());
    displayed_ = std::move(next);
    return frames;
  }

} // namespace signlang::peripheral_service

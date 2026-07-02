#include "scrolling_display.hpp"

#include "serial_protocol.hpp"

#include <algorithm>
#include <utility>

namespace signlang::peripheral_service {
  namespace {

    constexpr auto kScrollGapPx = std::uint16_t{16};

  } // namespace

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
    update_widths();
    force_refresh_ = true;
  }

  void ScrollingDisplay::set_second_line(std::string text) {
    if (second_line_.text == text) {
      return;
    }
    second_line_.text = std::move(text);
    second_line_.offset = 0;
    update_widths();
    force_refresh_ = true;
  }

  void ScrollingDisplay::clear_second_line() { set_second_line({}); }

  auto ScrollingDisplay::tick(Clock::time_point now) -> std::vector<std::vector<std::uint8_t>> {
    const auto refresh_elapsed = last_refresh_at_ == Clock::time_point{} ||
                                 now - last_refresh_at_ >= std::chrono::milliseconds{options_.refresh_interval_ms};
    if (!refresh_elapsed && !force_refresh_) {
      return {};
    }

    advance_scroll(now);
    const auto frames = frames_for_current_dirty_region();
    last_refresh_at_ = now;
    force_refresh_ = false;
    return frames;
  }

  auto ScrollingDisplay::full_refresh() -> std::vector<std::vector<std::uint8_t>> {
    auto frames = std::vector<std::vector<std::uint8_t>>{};
    frames.push_back(make_clear_frame());
    auto next = OledFramebuffer{options_.width, options_.height};
    render(next);
    auto block_frames = split_draw_block(next.to_block(Rect{0, 0, options_.width, options_.height}));
    frames.insert(frames.end(), std::make_move_iterator(block_frames.begin()), std::make_move_iterator(block_frames.end()));
    frames.push_back(make_refresh_frame());
    displayed_ = std::move(next);
    force_refresh_ = false;
    return frames;
  }

  void ScrollingDisplay::update_widths() {
    first_line_.width = renderer_.measure_width(first_line_.text);
    second_line_.width = renderer_.measure_width(second_line_.text);
  }

  void ScrollingDisplay::advance_scroll(Clock::time_point now) {
    if (last_scroll_at_ != Clock::time_point{} &&
        now - last_scroll_at_ < std::chrono::milliseconds{options_.scroll_interval_ms}) {
      return;
    }

    auto changed = false;
    auto advance_line = [this, &changed](LineState& line) {
      if (line.width <= options_.width) {
        if (line.offset != 0U) {
          line.offset = 0;
          changed = true;
        }
        return;
      }
      const auto wrap_width = static_cast<std::uint16_t>(line.width + kScrollGapPx);
      line.offset = static_cast<std::uint16_t>((line.offset + options_.scroll_step_px) % wrap_width);
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
    const auto repeated_x = static_cast<std::int16_t>(line.width + kScrollGapPx - line.offset);
    if (repeated_x < options_.width) {
      renderer_.draw_text(framebuffer, line.text, repeated_x, y);
    }
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

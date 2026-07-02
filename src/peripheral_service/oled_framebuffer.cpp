#include "oled_framebuffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace signlang::peripheral_service {

  OledFramebuffer::OledFramebuffer(std::uint8_t width, std::uint8_t height) :
      width_{width}, height_{height}, pixels_(static_cast<std::size_t>(width) * height, 0) {
    if (width_ == 0 || height_ == 0 || (height_ % 8U) != 0U) {
      throw std::runtime_error("invalid OLED framebuffer geometry");
    }
  }

  auto OledFramebuffer::width() const -> std::uint8_t { return width_; }

  auto OledFramebuffer::height() const -> std::uint8_t { return height_; }

  auto OledFramebuffer::get(std::uint8_t x, std::uint8_t y) const -> bool {
    if (x >= width_ || y >= height_) {
      return false;
    }
    return pixels_[index(x, y)] != 0U;
  }

  void OledFramebuffer::set(std::uint8_t x, std::uint8_t y, bool value) {
    if (x >= width_ || y >= height_) {
      return;
    }
    pixels_[index(x, y)] = value ? 1U : 0U;
  }

  void OledFramebuffer::clear() { std::fill(pixels_.begin(), pixels_.end(), 0); }

  void OledFramebuffer::clear_rect(Rect rect) {
    const auto max_y = std::min<std::uint16_t>(height_, static_cast<std::uint16_t>(rect.y) + rect.height);
    const auto max_x = std::min<std::uint16_t>(width_, static_cast<std::uint16_t>(rect.x) + rect.width);
    for (auto y = rect.y; y < max_y; ++y) {
      for (auto x = rect.x; x < max_x; ++x) {
        set(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), false);
      }
    }
  }

  auto OledFramebuffer::to_block(Rect rect) const -> OledBlock {
    if ((rect.y % 8U) != 0U || (rect.height % 8U) != 0U) {
      throw std::runtime_error("OLED block rect must be page-aligned");
    }
    if (rect.width == 0 || rect.height == 0) {
      throw std::runtime_error("OLED block rect must not be empty");
    }

    const auto max_y = std::min<std::uint16_t>(height_, static_cast<std::uint16_t>(rect.y) + rect.height);
    const auto max_x = std::min<std::uint16_t>(width_, static_cast<std::uint16_t>(rect.x) + rect.width);
    auto data = std::vector<std::uint8_t>{};
    const auto page_count = (max_y - rect.y) / 8U;
    data.reserve(static_cast<std::size_t>(max_x - rect.x) * page_count);

    for (auto page = std::uint16_t{0}; page < page_count; ++page) {
      const auto page_y = static_cast<std::uint16_t>(rect.y) + page * 8U;
      for (auto x = rect.x; x < max_x; ++x) {
        auto column = std::uint8_t{0};
        for (auto bit = std::uint8_t{0}; bit < 8U; ++bit) {
          if (get(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(page_y + bit))) {
            column = static_cast<std::uint8_t>(column | (1U << bit));
          }
        }
        data.push_back(column);
      }
    }

    return OledBlock{rect.x, rect.y, static_cast<std::uint8_t>(max_x - rect.x),
                     static_cast<std::uint8_t>(max_y - rect.y), std::move(data)};
  }

  auto OledFramebuffer::diff_rect(const OledFramebuffer& other) const -> std::optional<Rect> {
    if (width_ != other.width_ || height_ != other.height_) {
      throw std::runtime_error("cannot compare OLED framebuffers with different geometry");
    }

    auto min_x = static_cast<std::uint16_t>(width_);
    auto min_y = static_cast<std::uint16_t>(height_);
    auto max_x = std::uint16_t{0};
    auto max_y = std::uint16_t{0};
    auto found = false;
    for (auto y = std::uint8_t{0}; y < height_; ++y) {
      for (auto x = std::uint8_t{0}; x < width_; ++x) {
        if (get(x, y) != other.get(x, y)) {
          found = true;
          min_x = std::min<std::uint16_t>(min_x, x);
          min_y = std::min<std::uint16_t>(min_y, y);
          max_x = std::max<std::uint16_t>(max_x, x);
          max_y = std::max<std::uint16_t>(max_y, y);
        }
      }
    }

    if (!found) {
      return std::nullopt;
    }
    return Rect{static_cast<std::uint8_t>(min_x), static_cast<std::uint8_t>(min_y),
                static_cast<std::uint8_t>(max_x - min_x + 1U), static_cast<std::uint8_t>(max_y - min_y + 1U)};
  }

  auto OledFramebuffer::page_aligned(Rect rect) const -> Rect {
    const auto y0 = static_cast<std::uint8_t>((rect.y / 8U) * 8U);
    const auto y1_raw = static_cast<std::uint16_t>(rect.y) + rect.height;
    const auto y1 = std::min<std::uint16_t>(height_, ((y1_raw + 7U) / 8U) * 8U);
    const auto x1 = std::min<std::uint16_t>(width_, static_cast<std::uint16_t>(rect.x) + rect.width);
    return Rect{rect.x, y0, static_cast<std::uint8_t>(x1 - rect.x), static_cast<std::uint8_t>(y1 - y0)};
  }

  auto OledFramebuffer::index(std::uint8_t x, std::uint8_t y) const -> std::size_t {
    return static_cast<std::size_t>(y) * width_ + x;
  }

} // namespace signlang::peripheral_service

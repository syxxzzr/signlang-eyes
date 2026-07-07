#include "serial_protocol.hpp"

#include <algorithm>
#include <stdexcept>

namespace signlang::peripheral_service {
  namespace {

    constexpr auto kHead1 = std::uint8_t{0xAA};
    constexpr auto kHead2 = std::uint8_t{0x55};

    [[nodiscard]] auto checksum(OledCommand command, std::uint8_t x, std::uint8_t y, std::uint8_t width,
                                std::uint8_t height, const std::vector<std::uint8_t>& data) -> std::uint8_t {
      auto sum = static_cast<std::uint32_t>(command) + x + y + width + height +
                 static_cast<std::uint32_t>(data.size());
      for (const auto byte : data) {
        sum += byte;
      }
      return static_cast<std::uint8_t>(sum & 0xFFU);
    }

    void validate_geometry(std::uint8_t x, std::uint8_t y, std::uint8_t width, std::uint8_t height) {
      if (width == 0 || height == 0) {
        return;
      }
      if (x >= kOledWidth || y >= kOledHeight || static_cast<std::uint16_t>(x) + width > kOledWidth ||
          static_cast<std::uint16_t>(y) + height > kOledHeight) {
        throw std::runtime_error("OLED block geometry is out of range");
      }
      if ((height % 8U) != 0U || (y % 8U) != 0U) {
        throw std::runtime_error("OLED block height and y must be page-aligned");
      }
    }

  } // namespace

  auto make_frame(OledCommand command, std::uint8_t x, std::uint8_t y, std::uint8_t width, std::uint8_t height,
                  const std::vector<std::uint8_t>& data) -> std::vector<std::uint8_t> {
    if (data.size() > kMaxFrameDataLength) {
      throw std::runtime_error("UART frame data exceeds 128 bytes");
    }
    validate_geometry(x, y, width, height);

    auto frame = std::vector<std::uint8_t>{};
    frame.reserve(9U + data.size());
    frame.push_back(kHead1);
    frame.push_back(kHead2);
    frame.push_back(static_cast<std::uint8_t>(command));
    frame.push_back(x);
    frame.push_back(y);
    frame.push_back(width);
    frame.push_back(height);
    frame.push_back(static_cast<std::uint8_t>(data.size()));
    frame.insert(frame.end(), data.begin(), data.end());
    frame.push_back(checksum(command, x, y, width, height, data));
    return frame;
  }

  auto make_clear_frame() -> std::vector<std::uint8_t> {
    return make_frame(OledCommand::Clear, 0, 0, 0, 0, {});
  }

  auto make_refresh_frame() -> std::vector<std::uint8_t> {
    return make_frame(OledCommand::Refresh, 0, 0, 0, 0, {});
  }

  auto make_motor_frame(bool enabled) -> std::vector<std::uint8_t> {
    return make_frame(OledCommand::Motor, 0, 0, 0, 0, {static_cast<std::uint8_t>(enabled ? 0 : 1)});
  }

  auto split_draw_block(const OledBlock& block) -> std::vector<std::vector<std::uint8_t>> {
    if (block.width == 0 || block.height == 0) {
      throw std::runtime_error("OLED draw block must not be empty");
    }
    validate_geometry(block.x, block.y, block.width, block.height);

    const auto page_count = static_cast<std::size_t>(block.height / 8U);
    const auto expected_size = static_cast<std::size_t>(block.width) * page_count;
    if (block.data.size() != expected_size) {
      throw std::runtime_error("OLED block data size does not match geometry");
    }

    auto frames = std::vector<std::vector<std::uint8_t>>{};
    auto remaining_width = static_cast<std::size_t>(block.width);
    auto source_x = std::size_t{0};
    while (remaining_width > 0) {
      const auto max_width_this_frame = std::max<std::size_t>(1U, kMaxFrameDataLength / page_count);
      const auto chunk_width = std::min(remaining_width, max_width_this_frame);

      auto chunk_data = std::vector<std::uint8_t>{};
      chunk_data.reserve(chunk_width * page_count);
      for (auto page = std::size_t{0}; page < page_count; ++page) {
        const auto page_offset = page * block.width;
        chunk_data.insert(chunk_data.end(), block.data.begin() + static_cast<std::ptrdiff_t>(page_offset + source_x),
                          block.data.begin() +
                              static_cast<std::ptrdiff_t>(page_offset + source_x + chunk_width));
      }

      frames.push_back(make_frame(OledCommand::DrawBlock, static_cast<std::uint8_t>(block.x + source_x), block.y,
                                  static_cast<std::uint8_t>(chunk_width), block.height, chunk_data));
      source_x += chunk_width;
      remaining_width -= chunk_width;
    }

    return frames;
  }

  auto parse_button_event(std::uint8_t value) -> std::optional<ButtonEvent> {
    switch (value) {
    case static_cast<std::uint8_t>(ButtonEvent::SingleClick):
      return ButtonEvent::SingleClick;
    case static_cast<std::uint8_t>(ButtonEvent::DoubleClick):
      return ButtonEvent::DoubleClick;
    default:
      return std::nullopt;
    }
  }

} // namespace signlang::peripheral_service

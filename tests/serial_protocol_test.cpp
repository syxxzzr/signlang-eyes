#include "peripheral_service/serial_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace signlang::peripheral_service;

TEST_CASE("OLED control frames have the expected wire representation") {
  CHECK(make_clear_frame() == std::vector<std::uint8_t>{0xAAU, 0x55U, 0x01U, 0U, 0U, 0U, 0U, 0U, 0x01U});
  CHECK(make_refresh_frame() == std::vector<std::uint8_t>{0xAAU, 0x55U, 0x02U, 0U, 0U, 0U, 0U, 0U, 0x02U});

  const auto enabled = make_motor_frame(true);
  const auto disabled = make_motor_frame(false);
  REQUIRE(enabled.size() == 10U);
  CHECK(enabled[8] == 0U);
  CHECK(disabled[8] == 1U);
}

TEST_CASE("large OLED blocks are split without changing their data order") {
  auto data = std::vector<std::uint8_t>(512U);
  for (auto index = std::size_t{0}; index < data.size(); ++index) {
    data[index] = static_cast<std::uint8_t>(index & 0xFFU);
  }

  const auto frames = split_draw_block(OledBlock{0U, 0U, 128U, 32U, data});

  REQUIRE(frames.size() == 4U);
  for (auto frame_index = std::size_t{0}; frame_index < frames.size(); ++frame_index) {
    const auto& frame = frames[frame_index];
    REQUIRE(frame.size() == 137U);
    CHECK(frame[0] == 0xAAU);
    CHECK(frame[1] == 0x55U);
    CHECK(frame[2] == static_cast<std::uint8_t>(OledCommand::DrawBlock));
    CHECK(frame[3] == frame_index * 32U);
    CHECK(frame[5] == 32U);
    CHECK(frame[6] == 32U);
    CHECK(frame[7] == 128U);
    for (auto page = std::size_t{0}; page < 4U; ++page) {
      for (auto column = std::size_t{0}; column < 32U; ++column) {
        CHECK(frame[8U + page * 32U + column] == data[page * 128U + frame_index * 32U + column]);
      }
    }
  }
}

TEST_CASE("OLED blocks validate geometry and payload size") {
  CHECK_THROWS_AS(split_draw_block(OledBlock{0U, 0U, 0U, 8U, {}}), std::runtime_error);
  CHECK_THROWS_AS(split_draw_block(OledBlock{127U, 0U, 2U, 8U, {1U, 2U}}), std::runtime_error);
  CHECK_THROWS_AS(split_draw_block(OledBlock{0U, 1U, 1U, 8U, {1U}}), std::runtime_error);
  CHECK_THROWS_AS(split_draw_block(OledBlock{0U, 0U, 2U, 8U, {1U}}), std::runtime_error);
}

TEST_CASE("button bytes are decoded only when recognized") {
  CHECK(parse_button_event(0xA1U) == ButtonEvent::SingleClick);
  CHECK(parse_button_event(0xA2U) == ButtonEvent::DoubleClick);
  CHECK_FALSE(parse_button_event(0x00U).has_value());
}

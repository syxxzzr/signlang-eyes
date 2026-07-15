#include "common/audio_ring_buffer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>

using namespace signlang::common;

namespace {

auto make_audio_frame(std::uint64_t sequence, std::int16_t first_sample)
    -> signlang::audio_frontend::AudioFrame {
  auto frame = signlang::audio_frontend::AudioFrame{};
  frame.sequence_number = sequence;
  frame.timestamp_ns = sequence * 1'000'000U;
  frame.sample_rate_hz = 1'000U;
  frame.publish_period_ms = 5U;
  frame.frame_count = 5U;
  frame.channel_count = signlang::audio_frontend::kDefaultChannelCount;
  frame.bits_per_sample = signlang::audio_frontend::kBitsPerSample;
  for (auto index = std::uint32_t{0}; index < frame.frame_count; ++index) {
    frame.samples[index] = static_cast<std::int16_t>(first_sample + static_cast<std::int16_t>(index));
  }
  return frame;
}

} // namespace

TEST_CASE("audio window helper calculations handle overlap bounds") {
  CHECK(samples_for_window_ms(16'000U, 25U) == 400U);
  CHECK(hop_samples_for_overlap(400U, 0.5) == 200U);
  CHECK(hop_samples_for_overlap(400U, -1.0) == 400U);
  CHECK(hop_samples_for_overlap(400U, 2.0) == 1U);
}

TEST_CASE("audio ring buffer validates construction and frame metadata") {
  CHECK_THROWS_AS(AudioRingBuffer(0U, 1'000U), std::runtime_error);

  auto buffer = AudioRingBuffer{8U, 1'000U};
  auto invalid_frame = make_audio_frame(1U, 0);
  invalid_frame.sample_rate_hz = 16'000U;

  CHECK_FALSE(buffer.push(invalid_frame));
}

TEST_CASE("audio ring buffer returns normalized samples and latest metadata") {
  auto buffer = AudioRingBuffer{8U, 1'000U};
  const auto frame = make_audio_frame(9U, 16'384);
  REQUIRE(buffer.push(frame));

  auto requested_start = std::optional<std::uint64_t>{};
  auto should_stop = std::atomic_bool{false};
  auto window = AudioWindow{};
  REQUIRE(buffer.wait_for_window(requested_start, 5U, 2U, should_stop, window));

  CHECK(window.start_sample_index == 0U);
  CHECK(window.end_sample_index == 5U);
  CHECK(window.samples.front() == Catch::Approx(0.5F));
  CHECK(window.samples.back() == Catch::Approx(16'388.0F / 32'768.0F));
  CHECK(window.latest_audio_sequence_number == 9U);
  CHECK(window.latest_audio_timestamp_ns == 9'000'000U);
  CHECK(window.latest_audio_sample_rate_hz == 1'000U);
  CHECK(window.latest_audio_frame_count == 5U);
}

TEST_CASE("audio ring buffer keeps the newest samples after wrapping") {
  auto buffer = AudioRingBuffer{8U, 1'000U};
  REQUIRE(buffer.push(make_audio_frame(1U, 0)));
  REQUIRE(buffer.push(make_audio_frame(2U, 5)));

  auto requested_start = std::optional<std::uint64_t>{0U};
  auto should_stop = std::atomic_bool{false};
  auto window = AudioWindow{};
  REQUIRE(buffer.wait_for_window(requested_start, 8U, 2U, should_stop, window));

  CHECK(window.start_sample_index == 2U);
  CHECK(window.end_sample_index == 10U);
  for (auto index = std::size_t{0}; index < window.samples.size(); ++index) {
    CHECK(window.samples[index] == Catch::Approx(static_cast<float>(index + 2U) / 32'768.0F));
  }
}

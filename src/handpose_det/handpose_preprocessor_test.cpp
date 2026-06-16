#include "handpose_preprocessor.hpp"

#include "video_frontend/video_frame.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

  auto make_metadata(std::uint32_t width, std::uint32_t height) -> signlang::video_frontend::VideoFrameMetadata {
    return signlang::video_frontend::VideoFrameMetadata{
        .sequence_number = 0,
        .timestamp_ns = 0,
        .capture_width = width,
        .capture_height = height,
        .output_width = width,
        .output_height = height,
        .fps = 30,
        .pixel_format = signlang::video_frontend::kPixelFormatYuyv,
        .payload_size_bytes = width * height * 2,
    };
  }

  auto expect_equal(std::uint8_t actual, std::uint8_t expected, const char* name) -> bool {
    if (actual != expected) {
      std::cerr << name << " expected " << static_cast<int>(expected) << " got " << static_cast<int>(actual) << '\n';
      return false;
    }
    return true;
  }

  auto test_yuyv_to_rgb() -> bool {
    signlang::handpose_det::HandPosePreprocessor preprocessor{2, 1};
    const auto metadata = make_metadata(2, 1);
    const std::array<std::uint8_t, 4> yuyv{16, 128, 235, 128};
    std::array<std::uint8_t, 6> rgb{};

    preprocessor.process(metadata, yuyv.data(), yuyv.size(), rgb.data(), 2);

    return expect_equal(rgb[0], 0, "black.r") && expect_equal(rgb[1], 0, "black.g") &&
           expect_equal(rgb[2], 0, "black.b") && expect_equal(rgb[3], 255, "white.r") &&
           expect_equal(rgb[4], 255, "white.g") && expect_equal(rgb[5], 255, "white.b");
  }

  auto test_letterbox_padding() -> bool {
    signlang::handpose_det::HandPosePreprocessor preprocessor{4, 4};
    const auto metadata = make_metadata(4, 2);
    const std::array<std::uint8_t, 16> yuyv{16, 128, 16, 128, 16, 128, 16, 128,
                                           16, 128, 16, 128, 16, 128, 16, 128};
    std::array<std::uint8_t, 4 * 4 * 3> rgb{};

    preprocessor.process(metadata, yuyv.data(), yuyv.size(), rgb.data(), 4);
    const auto letterbox = preprocessor.letterbox();

    return letterbox.resized_width == 4 && letterbox.resized_height == 2 && letterbox.y_pad == 1 &&
           expect_equal(rgb[0], 114, "pad.r") && expect_equal(rgb[1], 114, "pad.g") &&
           expect_equal(rgb[2], 114, "pad.b") && expect_equal(rgb[12], 0, "image.r");
  }

} // namespace

auto main() -> int {
  if (!test_yuyv_to_rgb() || !test_letterbox_padding()) {
    return 1;
  }

  return 0;
}

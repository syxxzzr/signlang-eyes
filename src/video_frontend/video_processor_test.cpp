#include "video_processor.hpp"

#include "video_frame.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

  void require_equal(std::uint8_t actual, std::uint8_t expected, const char* context) {
    if (actual != expected) {
      throw std::runtime_error(std::string(context) + ": expected " + std::to_string(expected) + ", got " +
                               std::to_string(actual));
    }
  }

  void test_yuyv_resize_4x2_to_2x1() {
    const signlang::video_frontend::VideoFormat capture_format{
        .width = 4,
        .height = 2,
        .pixel_format = signlang::video_frontend::kPixelFormatYuyv,
    };
    const signlang::video_frontend::VideoFormat output_format{
        .width = 2,
        .height = 1,
        .pixel_format = signlang::video_frontend::kPixelFormatYuyv,
    };

    const std::array<std::uint8_t, 16> input_frame{
        10, 50, 20, 60, 30, 70, 40, 80,
        90, 51, 91, 61, 92, 71, 93, 81,
    };
    std::array<std::uint8_t, 4> output_frame{};

    signlang::video_frontend::VideoProcessor processor{capture_format, output_format};
    const signlang::video_frontend::CapturedVideoFrame captured_frame{
        .data = input_frame.data(),
        .size_bytes = input_frame.size(),
    };
    processor.process(captured_frame, iox2::bb::MutableSlice<std::uint8_t>{output_frame.data(), output_frame.size()});

    require_equal(output_frame[0], 10, "Y0");
    require_equal(output_frame[1], 50, "U");
    require_equal(output_frame[2], 30, "Y1");
    require_equal(output_frame[3], 60, "V");
  }

} // namespace

auto main() -> int {
  try {
    test_yuyv_resize_4x2_to_2x1();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include "handpose_preprocessor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kRgbChannelCount = std::uint32_t{3};
    constexpr auto kYuyvBytesPerPixel = std::uint32_t{2};
    constexpr auto kLetterboxFill = std::uint8_t{114};

    auto checked_yuyv_size_bytes(std::uint32_t width, std::uint32_t height) -> std::uint64_t {
      return static_cast<std::uint64_t>(width) * height * kYuyvBytesPerPixel;
    }

    auto clamp_to_u8(int value) -> std::uint8_t {
      return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    }

    void yuv_to_rgb(std::uint8_t y, std::uint8_t u, std::uint8_t v, std::uint8_t* output) {
      const auto c = static_cast<int>(y) - 16;
      const auto d = static_cast<int>(u) - 128;
      const auto e = static_cast<int>(v) - 128;
      output[0] = clamp_to_u8((298 * c + 409 * e + 128) >> 8);
      output[1] = clamp_to_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
      output[2] = clamp_to_u8((298 * c + 516 * d + 128) >> 8);
    }

  } // namespace

  HandPosePreprocessor::HandPosePreprocessor(std::uint32_t model_width, std::uint32_t model_height) :
      model_width_{model_width}, model_height_{model_height}, image_width_{0}, image_height_{0},
      letterbox_{.resized_width = model_width, .resized_height = model_height, .x_pad = 0, .y_pad = 0, .scale = 1.0F} {
    if (model_width_ == 0 || model_height_ == 0) {
      throw std::runtime_error("RKNN model input dimensions must be non-zero");
    }
  }

  void HandPosePreprocessor::prepare(const signlang::video_frontend::VideoFrameMetadata& metadata) {
    if (metadata.pixel_format != signlang::video_frontend::kPixelFormatYuyv) {
      throw std::runtime_error("Hand pose detector currently supports YUYV video input only");
    }

    if (metadata.output_width == 0 || metadata.output_height == 0) {
      throw std::runtime_error("Invalid upstream video frame dimensions");
    }

    if ((metadata.output_width % 2) != 0) {
      throw std::runtime_error("YUYV video input width must be even");
    }

    if (metadata.output_width != image_width_ || metadata.output_height != image_height_) {
      rebuild_maps(metadata.output_width, metadata.output_height);
    }
  }

  void HandPosePreprocessor::process(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                     const std::uint8_t* input_data, std::uint64_t input_size_bytes,
                                     std::uint8_t* output_data, std::uint32_t output_stride_width_pixels) {
    prepare(metadata);

    const auto expected_input_size = checked_yuyv_size_bytes(metadata.output_width, metadata.output_height);
    if (input_size_bytes < expected_input_size) {
      throw std::runtime_error("Upstream YUYV video frame payload is smaller than metadata dimensions");
    }

    if (output_stride_width_pixels < model_width_) {
      throw std::runtime_error("RKNN input stride is smaller than model width");
    }

    const auto output_stride_bytes =
        static_cast<std::uint64_t>(output_stride_width_pixels) * kRgbChannelCount;
    std::memset(output_data, kLetterboxFill, static_cast<std::uint64_t>(model_height_) * output_stride_bytes);

    for (std::uint32_t y = 0; y < letterbox_.resized_height; ++y) {
      auto* output_row =
          output_data + (static_cast<std::uint64_t>(letterbox_.y_pad + y) * output_stride_bytes) +
          (static_cast<std::uint64_t>(letterbox_.x_pad) * kRgbChannelCount);
      const auto map_row_offset = static_cast<std::uint64_t>(y) * letterbox_.resized_width;

      for (std::uint32_t x = 0; x < letterbox_.resized_width; ++x) {
        const auto& mapping = pixel_maps_[map_row_offset + x];
        const auto* source_pair = input_data + mapping.source_yuyv_pair_offset;
        const auto y_value = mapping.second_luma ? source_pair[2] : source_pair[0];
        yuv_to_rgb(y_value, source_pair[1], source_pair[3], output_row + (static_cast<std::uint64_t>(x) * 3));
      }
    }
  }

  auto HandPosePreprocessor::letterbox() const -> LetterboxInfo { return letterbox_; }

  void HandPosePreprocessor::rebuild_maps(std::uint32_t image_width, std::uint32_t image_height) {
    const auto scale_w = static_cast<float>(model_width_) / static_cast<float>(image_width);
    const auto scale_h = static_cast<float>(model_height_) / static_cast<float>(image_height);
    const auto scale = std::min(scale_w, scale_h);

    auto resized_width = static_cast<std::uint32_t>(static_cast<float>(image_width) * scale);
    auto resized_height = static_cast<std::uint32_t>(static_cast<float>(image_height) * scale);
    resized_width = std::max<std::uint32_t>(1, resized_width);
    resized_height = std::max<std::uint32_t>(1, resized_height);

    letterbox_ = LetterboxInfo{
        .resized_width = resized_width,
        .resized_height = resized_height,
        .x_pad = (model_width_ - resized_width) / 2,
        .y_pad = (model_height_ - resized_height) / 2,
        .scale = scale,
    };

    const auto map_size = static_cast<std::uint64_t>(letterbox_.resized_width) * letterbox_.resized_height;
    if (map_size > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("Hand pose preprocessor map is too large");
    }

    pixel_maps_.resize(map_size);
    for (std::uint32_t output_y = 0; output_y < letterbox_.resized_height; ++output_y) {
      const auto source_y = static_cast<std::uint32_t>(
          (static_cast<std::uint64_t>(output_y) * image_height) / letterbox_.resized_height);
      for (std::uint32_t output_x = 0; output_x < letterbox_.resized_width; ++output_x) {
        const auto source_x = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(output_x) * image_width) / letterbox_.resized_width);
        const auto source_pair_x = source_x / 2;
        const auto source_offset =
            (static_cast<std::uint64_t>(source_y) * image_width + static_cast<std::uint64_t>(source_pair_x) * 2) * 2;
        if (source_offset > std::numeric_limits<std::uint32_t>::max()) {
          throw std::runtime_error("YUYV source frame is too large");
        }

        pixel_maps_[static_cast<std::uint64_t>(output_y) * letterbox_.resized_width + output_x] = PixelMap{
            .source_yuyv_pair_offset = static_cast<std::uint32_t>(source_offset),
            .second_luma = (source_x % 2) != 0,
        };
      }
    }

    image_width_ = image_width;
    image_height_ = image_height;
  }

} // namespace signlang::handpose_det

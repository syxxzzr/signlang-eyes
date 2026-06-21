#include "handpose_preprocessor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kRgbChannelCount = std::uint32_t{3};
    constexpr auto kLetterboxFill = std::uint8_t{114};

    auto checked_rgb_size_bytes(std::uint32_t width, std::uint32_t height) -> std::uint64_t {
      return static_cast<std::uint64_t>(width) * height * kRgbChannelCount;
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
    if (metadata.pixel_format != signlang::video_frontend::kPixelFormatRgb24) {
      throw std::runtime_error("Hand pose detector currently supports RGB24 video input only");
    }

    if (metadata.output_width == 0 || metadata.output_height == 0) {
      throw std::runtime_error("Invalid upstream video frame dimensions");
    }

    if (metadata.output_width != image_width_ || metadata.output_height != image_height_) {
      rebuild_maps(metadata.output_width, metadata.output_height);
    }
  }

  void HandPosePreprocessor::process(const signlang::video_frontend::VideoFrameMetadata& metadata,
                                     const std::uint8_t* input_data, std::uint64_t input_size_bytes,
                                     std::uint8_t* output_data, std::uint32_t output_stride_width_pixels) {
    prepare(metadata);

    const auto expected_input_size = checked_rgb_size_bytes(metadata.output_width, metadata.output_height);
    if (input_size_bytes < expected_input_size) {
      throw std::runtime_error("Upstream RGB video frame payload is smaller than metadata dimensions");
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
        const auto* source_pixel = input_data + mapping.source_rgb_offset;
        auto* output_pixel = output_row + (static_cast<std::uint64_t>(x) * kRgbChannelCount);
        output_pixel[0] = source_pixel[0];
        output_pixel[1] = source_pixel[1];
        output_pixel[2] = source_pixel[2];
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
        const auto source_offset =
            (static_cast<std::uint64_t>(source_y) * image_width + source_x) * kRgbChannelCount;
        if (source_offset > std::numeric_limits<std::uint32_t>::max()) {
          throw std::runtime_error("RGB source frame is too large");
        }

        pixel_maps_[static_cast<std::uint64_t>(output_y) * letterbox_.resized_width + output_x] = PixelMap{
            .source_rgb_offset = static_cast<std::uint32_t>(source_offset),
        };
      }
    }

    image_width_ = image_width;
    image_height_ = image_height;
  }

} // namespace signlang::handpose_det

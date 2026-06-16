#ifndef SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_PREPROCESSOR_HPP
#define SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_PREPROCESSOR_HPP

#include "video_frontend/video_frame.hpp"

#include <cstdint>
#include <vector>

namespace signlang::handpose_det {

  struct LetterboxInfo {
    std::uint32_t resized_width;
    std::uint32_t resized_height;
    std::uint32_t x_pad;
    std::uint32_t y_pad;
    float scale;
  };

  class HandPosePreprocessor {
  public:
    HandPosePreprocessor(std::uint32_t model_width, std::uint32_t model_height);

    void prepare(const signlang::video_frontend::VideoFrameMetadata& metadata);
    void process(const signlang::video_frontend::VideoFrameMetadata& metadata, const std::uint8_t* input_data,
                 std::uint64_t input_size_bytes, std::uint8_t* output_data, std::uint32_t output_stride_width_pixels);

    auto letterbox() const -> LetterboxInfo;

  private:
    struct PixelMap {
      std::uint32_t source_yuyv_pair_offset;
      bool second_luma;
    };

    void rebuild_maps(std::uint32_t image_width, std::uint32_t image_height);

    std::uint32_t model_width_;
    std::uint32_t model_height_;
    std::uint32_t image_width_;
    std::uint32_t image_height_;
    LetterboxInfo letterbox_;
    std::vector<PixelMap> pixel_maps_;
  };

} // namespace signlang::handpose_det

#endif // SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_HANDPOSE_PREPROCESSOR_HPP

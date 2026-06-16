#ifndef SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_FRAME_HPP
#define SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_FRAME_HPP

#include <cstdint>
#include <type_traits>

namespace signlang::video_frontend {

  constexpr auto kPixelFormatYuyv = std::uint32_t{0x56595559};
  constexpr auto kPixelFormatMjpeg = std::uint32_t{0x47504A4D};

  struct VideoFrameMetadata {
    std::uint64_t sequence_number;
    std::uint64_t timestamp_ns;
    std::uint32_t capture_width;
    std::uint32_t capture_height;
    std::uint32_t output_width;
    std::uint32_t output_height;
    std::uint32_t fps;
    std::uint32_t pixel_format;
    std::uint32_t payload_size_bytes;
  };

  static_assert(std::is_trivially_copyable_v<VideoFrameMetadata>);

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_FRAME_HPP

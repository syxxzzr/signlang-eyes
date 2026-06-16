#ifndef SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_FORMAT_HPP
#define SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_FORMAT_HPP

#include <cstdint>
#include <optional>

namespace signlang::video_frontend {

  constexpr std::uint32_t kDefaultFps = 30;
  constexpr std::uint32_t kMinDimension = 1;
  constexpr std::uint32_t kMaxDimension = 3840;
  constexpr std::uint32_t kMaxFps = 240;

  struct VideoFormatRequest {
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
  };

  struct VideoFormat {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pixel_format;
  };

} // namespace signlang::video_frontend

#endif // SIGNLANG_EYES_EDGEAI_VIDEO_FRONTEND_VIDEO_FORMAT_HPP

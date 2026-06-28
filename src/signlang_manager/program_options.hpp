#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::signlang_manager {

  constexpr const char* kDefaultBluetoothName = "SignLang Eyes";
  constexpr const char* kDefaultAdapterPath = "/org/bluez/hci0";
  constexpr const char* kDefaultModelPath = "models/bilstm/biltsm.rknn";
  constexpr const char* kDefaultPrototypesPath = "conf/prototypes.sqlite";
  constexpr auto kDefaultMinConfidence = float{0.3F};
  constexpr auto kDefaultMotionWeight = float{0.0F};
  constexpr auto kDefaultSubscriberBufferSize = std::uint64_t{2};
  constexpr auto kDefaultStreamFps = std::uint32_t{30};
  constexpr auto kDefaultMaxNotifyPayload = std::uint32_t{180};
  constexpr auto kDefaultMaxUploadBytes = std::uint32_t{8U * 1024U * 1024U};
  constexpr auto kDefaultUploadWindowOverlap = float{0.5F};

  struct ProgramOptions {
    std::string input_service_name;
    std::string signlang_result_service_name;
    std::string signlang_control_service_name;
    std::string bluetooth_name;
    std::string adapter_path;
    std::string model_path;
    std::string prototypes_path;
    float min_keypoint_confidence;
    float motion_weight;
    float upload_window_overlap;
    std::uint64_t subscriber_buffer_size;
    std::uint32_t stream_fps;
    std::uint32_t max_notify_payload;
    std::uint32_t max_upload_bytes;
    rknn_core_mask npu_core_mask;
    bool enable_streaming_by_default;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;
  auto parse_npu_core_mask(const std::string& core_str) -> rknn_core_mask;

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_PROGRAM_OPTIONS_HPP

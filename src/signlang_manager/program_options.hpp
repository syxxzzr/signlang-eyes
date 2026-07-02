#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include <string>
#include <variant>

namespace signlang::signlang_manager {

  constexpr const char* kDefaultBluetoothName = "SignLang Eyes";
  constexpr const char* kDefaultAdapterPath = "/org/bluez/hci0";
  constexpr auto kDefaultSubscriberBufferSize = std::uint64_t{2};
  constexpr auto kDefaultStreamFps = std::uint32_t{30};
  constexpr auto kDefaultMaxNotifyPayload = std::uint32_t{180};
  constexpr auto kDefaultMaxUploadBytes = std::uint32_t{8U * 1024U * 1024U};

  struct ProgramOptions {
    std::string input_service_name;
    std::string signlang_result_service_name;
    std::string gesture_management_service_name;
    std::string bluetooth_name;
    std::string adapter_path;
    std::uint64_t subscriber_buffer_size;
    std::uint32_t stream_fps;
    std::uint32_t max_notify_payload;
    std::uint32_t max_upload_bytes;
    bool enable_streaming_by_default;
    signlang::logging::Options logging;
  };

  struct ProgramUsage {
    std::string text;
  };

  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_PROGRAM_OPTIONS_HPP

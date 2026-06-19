#ifndef SIGNLANG_EYES_SIGNLANG_DET_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_PROGRAM_OPTIONS_HPP

#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::signlang_det {

constexpr const char* kDefaultModelPath = "models/signlang/signlang.rknn";
constexpr const char* kDefaultLabelMapPath = "models/signlang/labels.txt";
constexpr const char* kDefaultPrototypesPath = "models/signlang/prototypes.bin";
constexpr auto kDefaultSequenceLength = std::uint32_t{30};
constexpr auto kDefaultOverlapRatio = float{0.2F};
constexpr auto kDefaultMinConfidence = float{0.3F};
constexpr auto kDefaultSubscriberBufferSize = std::uint64_t{2};
constexpr auto kDefaultMotionWeight = float{0.0F};
constexpr auto kDefaultDtwWindowRatio = float{0.5F};
constexpr auto kDefaultConfidenceThreshold = float{0.6F};
constexpr auto kDefaultConfidenceMargin = float{0.1F};

struct ProgramOptions {
  std::string input_service_name;
  std::string output_service_name;
  std::string state_event_service_name;
  std::string state_blackboard_service_name;
  std::string model_path;
  std::string label_map_path;
  std::string prototypes_path;
  std::uint32_t sequence_length;
  float overlap_ratio;
  float min_keypoint_confidence;
  std::uint64_t subscriber_buffer_size;
  rknn_core_mask npu_core_mask;
  float motion_weight;
  float dtw_window_ratio;
  float confidence_threshold;
  float confidence_margin;
};

struct ProgramUsage {
  std::string text;
};

using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;
auto parse_npu_core_mask(const std::string& core_str) -> rknn_core_mask;

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_PROGRAM_OPTIONS_HPP

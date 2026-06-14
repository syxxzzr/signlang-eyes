#ifndef SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_PROGRAM_OPTIONS_HPP

#include "rknn_api.h"

#include <cstdint>
#include <string>
#include <variant>

namespace signlang::signlang_det {

constexpr const char* kDefaultModelPath = "models/signlang-lstm/gesture_lstm.rknn";
constexpr const char* kDefaultLabelMapPath = "models/signlang-lstm/labels.txt";
constexpr std::uint32_t kDefaultSequenceLength = 30;
constexpr float kDefaultOverlapRatio = 0.2f;
constexpr float kDefaultMinConfidence = 0.3f;
constexpr std::uint64_t kDefaultSubscriberBufferSize = 2;

struct ProgramOptions {
  std::string input_service_name;
  std::string output_service_name;
  std::string model_path;
  std::string label_map_path;
  std::uint32_t sequence_length;
  float overlap_ratio;
  float min_keypoint_confidence;
  std::uint64_t subscriber_buffer_size;
  rknn_core_mask npu_core_mask;
};

struct ProgramUsage {
  std::string text;
};

using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;
auto parse_npu_core_mask(const std::string& core_str) -> rknn_core_mask;

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_PROGRAM_OPTIONS_HPP

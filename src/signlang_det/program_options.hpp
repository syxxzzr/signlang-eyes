#ifndef SIGNLANG_EYES_SIGNLANG_DET_PROGRAM_OPTIONS_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_PROGRAM_OPTIONS_HPP

#include "common/logging.hpp"
#include "gesture_pipeline.hpp"
#include "signlang_model.hpp"
#include "rknn_api.h"

#include <optional>
#include <string>
#include <variant>

namespace signlang::signlang_det {

  constexpr const char* kDefaultModelPath = "models/signlang/temporal_encoder.rknn";
  constexpr const char* kDefaultPrototypesPath = "conf/prototypes.sqlite";

  struct ProgramOptions {
    std::string input_service_name;
    std::string output_service_name;
    std::optional<std::string> prototype_control_service_name;
    std::optional<std::string> gesture_management_service_name;
    std::string model_path;
    std::string prototypes_path;
    float min_keypoint_confidence;
    std::uint64_t subscriber_buffer_size;
    rknn_core_mask npu_core_mask;
    SegmenterOptions segmenter;
    PreprocessingOptions preprocessing;
    MatcherOptions matcher;
    std::uint32_t segment_queue_capacity;
    std::uint32_t duplicate_suppression_ms;
    std::uint32_t max_representative_samples;
    bool publish_rejections;
    signlang::logging::Options logging;
  };

  struct ProgramUsage { std::string text; };
  using ProgramOptionsParseResult = std::variant<ProgramOptions, ProgramUsage>;

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult;
  auto parse_npu_core_mask(const std::string& core_str) -> rknn_core_mask;

} // namespace signlang::signlang_det

#endif

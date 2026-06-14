#include "program_options.hpp"

#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::signlang_det {

auto parse_npu_core_mask(const std::string& core_str) -> rknn_core_mask {
  if (core_str == "auto") {
    return RKNN_NPU_CORE_AUTO;
  }
  if (core_str == "0") {
    return RKNN_NPU_CORE_0;
  }
  if (core_str == "1") {
    return RKNN_NPU_CORE_1;
  }
  if (core_str == "2") {
    return RKNN_NPU_CORE_2;
  }
  if (core_str == "0_1") {
    return RKNN_NPU_CORE_0_1;
  }
  if (core_str == "0_1_2") {
    return RKNN_NPU_CORE_0_1_2;
  }
  if (core_str == "all") {
    return RKNN_NPU_CORE_ALL;
  }
  throw std::runtime_error("Invalid NPU core: " + core_str +
                           ". Valid options: auto, 0, 1, 2, 0_1, 0_1_2, all");
}

auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
  cxxopts::Options options{
    "signlang_eyes_edgeai_signlang_det",
    "Subscribe handpose detections, extract temporal features, run RKNN sign language recognition"};

  options.add_options()
    ("i,input-service", "Input handpose iceoryx2 service name",
     cxxopts::value<std::string>())
    ("o,output-service", "Output signlang result iceoryx2 service name",
     cxxopts::value<std::string>())
    ("m,model", "RKNN model path",
     cxxopts::value<std::string>()->default_value(kDefaultModelPath))
    ("label-map", "Gesture label map file",
     cxxopts::value<std::string>()->default_value(kDefaultLabelMapPath))
    ("sequence-length", "Sliding window frame count",
     cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultSequenceLength)))
    ("overlap-ratio", "Window overlap ratio (0.0-0.9)",
     cxxopts::value<float>()->default_value(std::to_string(kDefaultOverlapRatio)))
    ("min-confidence", "Minimum keypoint confidence (0.0-1.0)",
     cxxopts::value<float>()->default_value(std::to_string(kDefaultMinConfidence)))
    ("subscriber-buffer", "iceoryx2 subscriber queue size (1-8)",
     cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))
    ("npu-core", "NPU core selection: auto,0,1,2,0_1,0_1_2,all",
     cxxopts::value<std::string>()->default_value("auto"))
    ("h,help", "Print usage");

  const auto parsed_options = options.parse(argc, argv);
  if (parsed_options.count("help") != 0) {
    return ProgramUsage{.text = options.help()};
  }

  if (parsed_options.count("input-service") == 0 ||
      parsed_options.count("output-service") == 0) {
    throw std::runtime_error(
      "Both --input-service and --output-service are required.\n\n" + options.help());
  }

  const auto sequence_length = parsed_options["sequence-length"].as<std::uint32_t>();
  if (sequence_length == 0 || sequence_length > 120) {
    throw std::runtime_error("--sequence-length must be between 1 and 120");
  }

  const auto overlap_ratio = parsed_options["overlap-ratio"].as<float>();
  if (overlap_ratio < 0.0f || overlap_ratio >= 1.0f) {
    throw std::runtime_error("--overlap-ratio must be in [0.0, 1.0)");
  }

  const auto min_confidence = parsed_options["min-confidence"].as<float>();
  if (min_confidence < 0.0f || min_confidence > 1.0f) {
    throw std::runtime_error("--min-confidence must be in [0.0, 1.0]");
  }

  const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
  if (subscriber_buffer_size == 0 || subscriber_buffer_size > 8) {
    throw std::runtime_error("--subscriber-buffer must be between 1 and 8");
  }

  const auto npu_core_str = parsed_options["npu-core"].as<std::string>();
  const auto npu_core_mask = parse_npu_core_mask(npu_core_str);

  return ProgramOptions{
    .input_service_name = parsed_options["input-service"].as<std::string>(),
    .output_service_name = parsed_options["output-service"].as<std::string>(),
    .model_path = parsed_options["model"].as<std::string>(),
    .label_map_path = parsed_options["label-map"].as<std::string>(),
    .sequence_length = sequence_length,
    .overlap_ratio = overlap_ratio,
    .min_keypoint_confidence = min_confidence,
    .subscriber_buffer_size = subscriber_buffer_size,
    .npu_core_mask = npu_core_mask
  };
}

} // namespace signlang::signlang_det

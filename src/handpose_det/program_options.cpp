#include "program_options.hpp"

#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::handpose_det {
  namespace {

    void validate_threshold(float value, const char* option_name) {
      if (value <= 0.0F || value >= 1.0F) {
        throw std::runtime_error(std::string("--") + option_name + " must be between 0 and 1");
      }
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_edgeai_handpose_det",
        "Subscribe video frames from iceoryx2, run YOLOv8 hand pose on RKNN NPU, and publish hand keypoints."};

    options.add_options()("i,input-service", "Upstream video iceoryx2 service name",
                          cxxopts::value<std::string>())(
        "o,output-service", "Output handpose iceoryx2 service name", cxxopts::value<std::string>())(
        "m,model", "RKNN model path", cxxopts::value<std::string>()->default_value(kDefaultModelPath))(
        "rknn-runtime", "RKNN runtime library path, empty means librknnrt.so from system loader",
        cxxopts::value<std::string>()->default_value(""))(
        "confidence", "Detection confidence threshold",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultConfidenceThreshold)))(
        "nms", "NMS IoU threshold", cxxopts::value<float>()->default_value(std::to_string(kDefaultNmsThreshold)))(
        "subscriber-buffer", "iceoryx2 subscriber queue size",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "keypoints", "Expected keypoint count per detection",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultKeypointCount)))(
        "max-detections", "Maximum detections published per frame",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxDetections)))(
        "npu-core", "RK3588 NPU core mask: auto,0,1,2,0_1,0_1_2,all",
        cxxopts::value<std::string>()->default_value("auto"))("verbose", "Print model tensor details")("h,help",
                                                                                                       "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{.text = options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("output-service") == 0) {
      throw std::runtime_error("Both --input-service and --output-service are required.\n\n" + options.help());
    }

    const auto confidence_threshold = parsed_options["confidence"].as<float>();
    const auto nms_threshold = parsed_options["nms"].as<float>();
    validate_threshold(confidence_threshold, "confidence");
    validate_threshold(nms_threshold, "nms");

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0 || subscriber_buffer_size > 8) {
      throw std::runtime_error("--subscriber-buffer must be between 1 and 8");
    }

    const auto keypoint_count = parsed_options["keypoints"].as<std::uint32_t>();
    if (keypoint_count == 0 || keypoint_count > kHandPoseKeypointCount) {
      throw std::runtime_error("--keypoints must be between 1 and " + std::to_string(kHandPoseKeypointCount));
    }

    const auto max_detections = parsed_options["max-detections"].as<std::uint32_t>();
    if (max_detections == 0 || max_detections > kMaxHandPoseDetections) {
      throw std::runtime_error("--max-detections must be between 1 and " + std::to_string(kMaxHandPoseDetections));
    }

    return ProgramOptionsParseResult{ProgramOptions{
        .input_service_name = parsed_options["input-service"].as<std::string>(),
        .output_service_name = parsed_options["output-service"].as<std::string>(),
        .model_path = parsed_options["model"].as<std::string>(),
        .rknn_runtime_library_path = parsed_options["rknn-runtime"].as<std::string>(),
        .confidence_threshold = confidence_threshold,
        .nms_threshold = nms_threshold,
        .subscriber_buffer_size = subscriber_buffer_size,
        .keypoint_count = keypoint_count,
        .max_detections = max_detections,
        .npu_core_mask = parsed_options["npu-core"].as<std::string>(),
        .verbose = parsed_options.count("verbose") != 0,
    }};
  }

} // namespace signlang::handpose_det

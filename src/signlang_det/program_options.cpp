#include "program_options.hpp"

#include "common/logging_cli.hpp"
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
    throw std::runtime_error("Invalid NPU core: " + core_str + ". Valid options: auto, 0, 1, 2, 0_1, 0_1_2, all");
  }

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{
        "signlang_eyes_signlang_det",
        "Subscribe handpose detections, extract temporal features, run BiLSTM+DTW sign language recognition"};

    options.add_options()("i,input-service", "Input handpose iceoryx2 service name", cxxopts::value<std::string>())(
        "o,output-service", "Output signlang result iceoryx2 service name", cxxopts::value<std::string>())(
        "prototype-control-service", "iceoryx2 request-response service for prototype reload control",
        cxxopts::value<std::string>())(
        "gesture-management-service", "iceoryx2 request-response service for gesture library management",
        cxxopts::value<std::string>())("m,model", "RKNN BiLSTM encoder model path",
                                       cxxopts::value<std::string>()->default_value(kDefaultModelPath))(
        "prototypes", "Gesture prototype SQLite database file",
        cxxopts::value<std::string>()->default_value(kDefaultPrototypesPath))(
        "sequence-length", "Sliding window frame count",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultSequenceLength)))(
        "overlap-ratio", "Window overlap ratio (0.0-0.9)",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultOverlapRatio)))(
        "min-confidence", "Minimum keypoint confidence (0.0-1.0)",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultMinConfidence)))(
        "subscriber-buffer", "iceoryx2 subscriber queue size (1-8)",
        cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))(
        "motion-weight", "Motion feature weight (0.0=ignore speed)",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultMotionWeight)))(
        "dtw-window-ratio", "DTW window ratio (0.0-1.0, larger=more tolerant)",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultDtwWindowRatio)))(
        "confidence-threshold", "Minimum confidence for recognition (0.0-1.0)",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultConfidenceThreshold)))(
        "confidence-margin", "Minimum margin between top1 and top2 (0.0-1.0)",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultConfidenceMargin)))(
        "duplicate-suppression-ms",
        "Suppress publishing the same recognized gesture again within this many milliseconds (0=disabled)",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultDuplicateSuppressionMs)))(
        "upload-window-overlap", "Overlap ratio when long gesture uploads are split into prototype samples",
        cxxopts::value<float>()->default_value(std::to_string(kDefaultUploadWindowOverlap)))(
        "max-representative-samples", "Maximum retained representative prototype samples per gesture",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxRepresentativeSamples)))(
        "consecutive-hit-windows", "Recognized gesture windows required before publishing a result",
        cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultConsecutiveHitWindows)))(
        "npu-core", "NPU core selection: auto,0,1,2,0_1,0_1_2,all",
        cxxopts::value<std::string>()->default_value("auto"))("h,help", "Print usage");
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("output-service") == 0) {
      throw std::runtime_error("Options --input-service and --output-service are required.\n\n" + options.help());
    }

    const auto sequence_length = parsed_options["sequence-length"].as<std::uint32_t>();
    if (sequence_length == 0) {
      throw std::runtime_error("--sequence-length must be greater than 0");
    }

    const auto overlap_ratio = parsed_options["overlap-ratio"].as<float>();
    if (overlap_ratio < 0.0F || overlap_ratio >= 1.0F) {
      throw std::runtime_error("--overlap-ratio must be in [0.0, 1.0)");
    }

    const auto min_confidence = parsed_options["min-confidence"].as<float>();
    if (min_confidence < 0.0F || min_confidence > 1.0F) {
      throw std::runtime_error("--min-confidence must be in [0.0, 1.0]");
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }

    const auto motion_weight = parsed_options["motion-weight"].as<float>();
    if (motion_weight < 0.0F || motion_weight > 1.0F) {
      throw std::runtime_error("--motion-weight must be in [0.0, 1.0]");
    }

    const auto dtw_window_ratio = parsed_options["dtw-window-ratio"].as<float>();
    if (dtw_window_ratio < 0.0F || dtw_window_ratio > 1.0F) {
      throw std::runtime_error("--dtw-window-ratio must be in [0.0, 1.0]");
    }

    const auto confidence_threshold = parsed_options["confidence-threshold"].as<float>();
    if (confidence_threshold < 0.0F || confidence_threshold > 1.0F) {
      throw std::runtime_error("--confidence-threshold must be in [0.0, 1.0]");
    }

    const auto confidence_margin = parsed_options["confidence-margin"].as<float>();
    if (confidence_margin < 0.0F || confidence_margin > 1.0F) {
      throw std::runtime_error("--confidence-margin must be in [0.0, 1.0]");
    }

    const auto duplicate_suppression_ms = parsed_options["duplicate-suppression-ms"].as<std::uint32_t>();

    const auto upload_window_overlap = parsed_options["upload-window-overlap"].as<float>();
    if (upload_window_overlap < 0.0F || upload_window_overlap >= 1.0F) {
      throw std::runtime_error("--upload-window-overlap must be in [0.0, 1.0)");
    }

    const auto max_representative_samples = parsed_options["max-representative-samples"].as<std::uint32_t>();
    if (max_representative_samples == 0) {
      throw std::runtime_error("--max-representative-samples must be greater than 0");
    }

    const auto consecutive_hit_windows = parsed_options["consecutive-hit-windows"].as<std::uint32_t>();
    if (consecutive_hit_windows == 0) {
      throw std::runtime_error("--consecutive-hit-windows must be greater than 0");
    }

    const auto npu_core_str = parsed_options["npu-core"].as<std::string>();
    const auto npu_core_mask = parse_npu_core_mask(npu_core_str);

    return ProgramOptions{
        parsed_options["input-service"].as<std::string>(),
        parsed_options["output-service"].as<std::string>(),
        parsed_options.count("prototype-control-service") != 0
            ? std::optional<std::string>{parsed_options["prototype-control-service"].as<std::string>()}
            : std::nullopt,
        parsed_options.count("gesture-management-service") != 0
            ? std::optional<std::string>{parsed_options["gesture-management-service"].as<std::string>()}
            : std::nullopt,
        parsed_options["model"].as<std::string>(),
        parsed_options["prototypes"].as<std::string>(),
        sequence_length,
        overlap_ratio,
        min_confidence,
        subscriber_buffer_size,
        npu_core_mask,
        motion_weight,
        dtw_window_ratio,
        confidence_threshold,
        confidence_margin,
        duplicate_suppression_ms,
        upload_window_overlap,
        max_representative_samples,
        consecutive_hit_windows,
        signlang::logging::parse_cli_options(parsed_options)};
  }

} // namespace signlang::signlang_det

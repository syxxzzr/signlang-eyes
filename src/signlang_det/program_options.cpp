#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace signlang::signlang_det {

  auto parse_npu_core_mask(const std::string& value) -> rknn_core_mask {
    if (value == "auto") return RKNN_NPU_CORE_AUTO;
    if (value == "0") return RKNN_NPU_CORE_0;
    if (value == "1") return RKNN_NPU_CORE_1;
    if (value == "2") return RKNN_NPU_CORE_2;
    if (value == "0_1") return RKNN_NPU_CORE_0_1;
    if (value == "0_1_2") return RKNN_NPU_CORE_0_1_2;
    if (value == "all") return RKNN_NPU_CORE_ALL;
    throw std::runtime_error("invalid NPU core: " + value);
  }

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    auto options = cxxopts::Options{"signlang_eyes_signlang_det",
        "Segment hand motion, encode temporal features, and match dynamic gesture prototypes"};
    options.add_options()
        ("i,input-service", "Input handpose iceoryx2 service name", cxxopts::value<std::string>())
        ("o,output-service", "Output signlang result iceoryx2 service name", cxxopts::value<std::string>())
        ("prototype-control-service", "Prototype control service", cxxopts::value<std::string>())
        ("gesture-management-service", "Gesture management service", cxxopts::value<std::string>())
        ("m,model", "Temporal encoder RKNN model", cxxopts::value<std::string>()->default_value(kDefaultModelPath))
        ("prototypes", "Gesture prototype SQLite database", cxxopts::value<std::string>()->default_value(kDefaultPrototypesPath))
        ("min-confidence", "Minimum hand confidence", cxxopts::value<float>()->default_value("0.3"))
        ("subscriber-buffer", "Subscriber queue size", cxxopts::value<std::uint64_t>()->default_value("2"))
        ("segment-pre-roll", "Pre-roll frames", cxxopts::value<std::uint32_t>()->default_value("6"))
        ("segment-start-confirmation", "Motion start confirmation frames", cxxopts::value<std::uint32_t>()->default_value("3"))
        ("segment-end-confirmation", "Motion end confirmation frames", cxxopts::value<std::uint32_t>()->default_value("6"))
        ("segment-post-roll", "Post-roll frames", cxxopts::value<std::uint32_t>()->default_value("3"))
        ("segment-min-frames", "Minimum captured frames", cxxopts::value<std::uint32_t>()->default_value("12"))
        ("segment-max-frames", "Maximum captured frames", cxxopts::value<std::uint32_t>()->default_value("120"))
        ("segment-cooldown-frames", "Quiet cooldown frames", cxxopts::value<std::uint32_t>()->default_value("6"))
        ("segment-cooldown-ms", "Maximum cooldown milliseconds", cxxopts::value<std::uint32_t>()->default_value("300"))
        ("segment-start-motion", "Motion start threshold", cxxopts::value<float>()->default_value("0.08"))
        ("segment-end-motion", "Motion end threshold", cxxopts::value<float>()->default_value("0.03"))
        ("segment-static-presence", "Static hand presence threshold", cxxopts::value<float>()->default_value("0.6"))
        ("segment-motion-alpha", "Motion EMA alpha", cxxopts::value<float>()->default_value("0.5"))
        ("segment-min-present-ratio", "Minimum hand-present frame ratio", cxxopts::value<float>()->default_value("0.5"))
        ("segment-min-quality", "Minimum segment quality", cxxopts::value<float>()->default_value("0.15"))
        ("segment-queue", "Pending segment queue capacity", cxxopts::value<std::uint32_t>()->default_value("2"))
        ("search-top-k", "Coarse-search class count", cxxopts::value<std::uint32_t>()->default_value("20"))
        ("dtw-window-ratio", "DTW window ratio", cxxopts::value<float>()->default_value("0.2"))
        ("dtw-threshold", "Global DTW threshold", cxxopts::value<float>()->default_value("0.8"))
        ("coarse-threshold", "Global pooled distance threshold", cxxopts::value<float>()->default_value("0.5"))
        ("distance-margin", "Minimum Top1/Top2 DTW distance margin", cxxopts::value<float>()->default_value("0.05"))
        ("use-coarse-threshold", "Enable pooled distance rejection", cxxopts::value<bool>()->default_value("true")->implicit_value("true"))
        ("publish-rejections", "Publish rejected segments", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
        ("duplicate-suppression-ms", "Duplicate recognition suppression", cxxopts::value<std::uint32_t>()->default_value("1000"))
        ("max-representative-samples", "Maximum samples retained per gesture", cxxopts::value<std::uint32_t>()->default_value("5"))
        ("npu-core", "NPU core: auto,0,1,2,0_1,0_1_2,all", cxxopts::value<std::string>()->default_value("auto"))
        ("h,help", "Print usage");
    signlang::logging::add_cli_options(options);
    const auto parsed = options.parse(argc, argv);
    if (parsed.count("help") != 0) return ProgramUsage{options.help()};
    if (parsed.count("input-service") == 0 || parsed.count("output-service") == 0) {
      throw std::runtime_error("--input-service and --output-service are required\n\n" + options.help());
    }

    auto segmenter = SegmenterOptions{};
    segmenter.pre_roll_frames = parsed["segment-pre-roll"].as<std::uint32_t>();
    segmenter.start_confirmation_frames = parsed["segment-start-confirmation"].as<std::uint32_t>();
    segmenter.end_confirmation_frames = parsed["segment-end-confirmation"].as<std::uint32_t>();
    segmenter.post_roll_frames = parsed["segment-post-roll"].as<std::uint32_t>();
    segmenter.minimum_frames = parsed["segment-min-frames"].as<std::uint32_t>();
    segmenter.maximum_frames = parsed["segment-max-frames"].as<std::uint32_t>();
    segmenter.cooldown_quiet_frames = parsed["segment-cooldown-frames"].as<std::uint32_t>();
    segmenter.cooldown_ms = parsed["segment-cooldown-ms"].as<std::uint32_t>();
    segmenter.start_motion_threshold = parsed["segment-start-motion"].as<float>();
    segmenter.end_motion_threshold = parsed["segment-end-motion"].as<float>();
    segmenter.static_presence_threshold = parsed["segment-static-presence"].as<float>();
    segmenter.motion_ema_alpha = parsed["segment-motion-alpha"].as<float>();

    auto preprocessing = PreprocessingOptions{};
    preprocessing.minimum_frames = segmenter.minimum_frames;
    preprocessing.minimum_present_frame_ratio = parsed["segment-min-present-ratio"].as<float>();
    preprocessing.minimum_mean_confidence = parsed["min-confidence"].as<float>();
    preprocessing.minimum_quality = parsed["segment-min-quality"].as<float>();

    auto matcher = MatcherOptions{};
    matcher.top_k = parsed["search-top-k"].as<std::uint32_t>();
    matcher.dtw_window_ratio = parsed["dtw-window-ratio"].as<float>();
    matcher.global_dtw_threshold = parsed["dtw-threshold"].as<float>();
    matcher.global_coarse_threshold = parsed["coarse-threshold"].as<float>();
    matcher.margin_threshold = parsed["distance-margin"].as<float>();
    matcher.use_coarse_threshold = parsed["use-coarse-threshold"].as<bool>();

    const auto in_unit = [](float value) { return value >= 0.0F && value <= 1.0F; };
    if (!std::isfinite(preprocessing.minimum_mean_confidence) ||
        !std::isfinite(preprocessing.minimum_present_frame_ratio) || !std::isfinite(preprocessing.minimum_quality) ||
        !std::isfinite(segmenter.static_presence_threshold) || !std::isfinite(segmenter.motion_ema_alpha) ||
        !std::isfinite(segmenter.start_motion_threshold) || !std::isfinite(segmenter.end_motion_threshold) ||
        !in_unit(preprocessing.minimum_mean_confidence) || !in_unit(preprocessing.minimum_present_frame_ratio) ||
        !in_unit(preprocessing.minimum_quality) || !in_unit(segmenter.static_presence_threshold) ||
        !in_unit(segmenter.motion_ema_alpha) || segmenter.start_motion_threshold < segmenter.end_motion_threshold ||
        matcher.top_k == 0 || !std::isfinite(matcher.dtw_window_ratio) || matcher.dtw_window_ratio < 0.0F ||
        matcher.dtw_window_ratio > 1.0F || !std::isfinite(matcher.global_dtw_threshold) ||
        !std::isfinite(matcher.global_coarse_threshold) || !std::isfinite(matcher.margin_threshold) ||
        matcher.global_dtw_threshold < 0.0F || matcher.global_coarse_threshold < 0.0F || matcher.margin_threshold < 0.0F ||
        parsed["subscriber-buffer"].as<std::uint64_t>() == 0 || parsed["segment-queue"].as<std::uint32_t>() == 0 ||
        parsed["max-representative-samples"].as<std::uint32_t>() == 0) {
      throw std::runtime_error("invalid sign language detector option range");
    }
    static_cast<void>(GestureSegmenter{segmenter});
    static_cast<void>(SegmentPreprocessor{preprocessing});

    return ProgramOptions{
        parsed["input-service"].as<std::string>(), parsed["output-service"].as<std::string>(),
        parsed.count("prototype-control-service") ? std::optional<std::string>{parsed["prototype-control-service"].as<std::string>()} : std::nullopt,
        parsed.count("gesture-management-service") ? std::optional<std::string>{parsed["gesture-management-service"].as<std::string>()} : std::nullopt,
        parsed["model"].as<std::string>(), parsed["prototypes"].as<std::string>(),
        preprocessing.minimum_mean_confidence, parsed["subscriber-buffer"].as<std::uint64_t>(),
        parse_npu_core_mask(parsed["npu-core"].as<std::string>()), segmenter, preprocessing, matcher,
        parsed["segment-queue"].as<std::uint32_t>(), parsed["duplicate-suppression-ms"].as<std::uint32_t>(),
        parsed["max-representative-samples"].as<std::uint32_t>(), parsed["publish-rejections"].as<bool>(),
        signlang::logging::parse_cli_options(parsed)};
  }

} // namespace signlang::signlang_det

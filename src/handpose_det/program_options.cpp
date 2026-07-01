#include "program_options.hpp"

#include "common/logging_cli.hpp"
#include "cxxopts.hpp"

#include <stdexcept>
#include <string>

namespace signlang::handpose_det {
  namespace {

    auto parse_core_mask(const std::string& value) -> rknn_core_mask {
      if (value == "auto") {
        return RKNN_NPU_CORE_AUTO;
      }
      if (value == "0") {
        return RKNN_NPU_CORE_0;
      }
      if (value == "1") {
        return RKNN_NPU_CORE_1;
      }
      if (value == "2") {
        return RKNN_NPU_CORE_2;
      }
      if (value == "0_1") {
        return RKNN_NPU_CORE_0_1;
      }
      if (value == "0_1_2") {
        return RKNN_NPU_CORE_0_1_2;
      }
      if (value == "all") {
        return RKNN_NPU_CORE_ALL;
      }
      throw std::runtime_error("Unsupported NPU core value: " + value);
    }

  } // namespace

  auto parse_program_options(int argc, char** argv) -> ProgramOptionsParseResult {
    cxxopts::Options options{"signlang_eyes_handpose_det",
                             "Subscribe video frames from iceoryx2, run MediaPipe palm and hand landmark RKNN models, "
                             "and publish hand keypoints."};

    // clang-format off
    options.add_options()
      ("i,input-service",              "Upstream video iceoryx2 service name",                      cxxopts::value<std::string>())
      ("o,output-service",             "Output handpose iceoryx2 service name",                     cxxopts::value<std::string>())
      ("m,model",                      "Palm detector RKNN model path",                             cxxopts::value<std::string>()->default_value(kDefaultPalmDetectorModelPath))
      ("landmark-model",               "Hand landmark RKNN model path",                             cxxopts::value<std::string>()->default_value(kDefaultLandmarkModelPath))
      ("confidence",                   "Palm detection confidence threshold",                       cxxopts::value<float>()->default_value(std::to_string(kDefaultConfidenceThreshold)))
      ("presence-threshold",           "Hand presence confidence threshold",                        cxxopts::value<float>()->default_value(std::to_string(kDefaultPresenceThreshold)))
      ("tracking-threshold",           "Tracking confidence threshold for reusing ROI",             cxxopts::value<float>()->default_value(std::to_string(kDefaultTrackingThreshold)))
      ("nms-iou-threshold",            "NMS IoU threshold for weighted merge",                      cxxopts::value<float>()->default_value(std::to_string(kDefaultNmsIouThreshold)))
      ("tracking-iou-match",           "IoU threshold for matching detections to tracked hands",    cxxopts::value<float>()->default_value(std::to_string(kDefaultTrackingIoUMatchThreshold)))
      ("crop-expansion",               "Expansion factor for tracking crop from bounding box",      cxxopts::value<float>()->default_value(std::to_string(kDefaultCropExpansion)))
      ("base-roi-expansion",           "Base ROI expansion from palm keypoints",                    cxxopts::value<float>()->default_value(std::to_string(kDefaultBaseRoiExpansion)))
      ("small-hand-expansion",         "Expansion factor for small hands",                          cxxopts::value<float>()->default_value(std::to_string(kDefaultSmallHandExpansion)))
      ("large-hand-expansion",         "Expansion factor for large hands",                          cxxopts::value<float>()->default_value(std::to_string(kDefaultLargeHandExpansion)))
      ("small-hand-threshold",         "Pixel threshold below which a hand is considered small",    cxxopts::value<float>()->default_value(std::to_string(kDefaultSmallHandThreshold)))
      ("large-hand-threshold",         "Pixel threshold above which a hand is considered large",    cxxopts::value<float>()->default_value(std::to_string(kDefaultLargeHandThreshold)))
      ("boundary-margin",              "Distance to image edge triggering boundary shrink",          cxxopts::value<float>()->default_value(std::to_string(kDefaultBoundaryMargin)))
      ("boundary-min-factor",          "Minimum expansion multiplier at image boundary",             cxxopts::value<float>()->default_value(std::to_string(kDefaultBoundaryMinFactor)))
      ("euro-min-cutoff",              "One Euro Filter min cutoff frequency (Hz)",                  cxxopts::value<float>()->default_value(std::to_string(kDefaultEuroMinCutoff)))
      ("euro-beta",                    "One Euro Filter speed coefficient (beta)",                   cxxopts::value<float>()->default_value(std::to_string(kDefaultEuroBeta)))
      ("euro-d-cutoff",                "One Euro Filter derivative cutoff frequency (Hz)",           cxxopts::value<float>()->default_value(std::to_string(kDefaultEuroDCutoff)))
      ("handedness-threshold",         "Threshold for left/right hand classification",              cxxopts::value<float>()->default_value(std::to_string(kDefaultHandednessThreshold)))
      ("max-tracking-gap",             "Max frames gap before tracking is considered lost",         cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxTrackingGap)))
      ("max-stale-frames",             "Max frames before a stale tracked hand slot is reclaimed",  cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultMaxStaleFrames)))
      ("full-frame-interval",         "Full-frame palm detection interval in frames; first frame always runs; 0 disables periodic full-frame refresh", cxxopts::value<std::uint32_t>()->default_value(std::to_string(kDefaultFullFrameInterval)))
      ("subscriber-buffer",            "iceoryx2 subscriber queue size",                            cxxopts::value<std::uint64_t>()->default_value(std::to_string(kDefaultSubscriberBufferSize)))
      ("single-hand",                  "Recognize and publish one hand slot instead of two",        cxxopts::value<bool>()->default_value(kDefaultSingleHand ? "true" : "false")->implicit_value("true"))
      ("swap-handedness",              "Swap left/right handedness classification for mirrored cameras", cxxopts::value<bool>()->default_value(kDefaultSwapHandedness ? "true" : "false")->implicit_value("true"))
      ("npu-core",                     "RK3588 NPU core mask: auto,0,1,2,0_1,0_1_2,all",           cxxopts::value<std::string>()->default_value("auto"))
      ("palm-npu-core",                "Palm detector NPU core mask; defaults to --npu-core",       cxxopts::value<std::string>())
      ("landmark-npu-core",            "Hand landmark NPU core mask; defaults to --npu-core",       cxxopts::value<std::string>())
      ("verbose",                      "Print model tensor details")
      ("h,help",                       "Print usage");
    // clang-format on
    signlang::logging::add_cli_options(options);

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      return ProgramUsage{options.help()};
    }

    if (parsed_options.count("input-service") == 0 || parsed_options.count("output-service") == 0) {
      throw std::runtime_error("Options --input-service and --output-service are required.\n\n" + options.help());
    }

    const auto subscriber_buffer_size = parsed_options["subscriber-buffer"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--subscriber-buffer must be greater than 0");
    }

    const auto single_hand = parsed_options["single-hand"].as<bool>();

    const auto default_npu_core = parsed_options["npu-core"].as<std::string>();
    const auto palm_npu_core = parsed_options.count("palm-npu-core") != 0
        ? parsed_options["palm-npu-core"].as<std::string>()
        : default_npu_core;
    const auto landmark_npu_core = parsed_options.count("landmark-npu-core") != 0
        ? parsed_options["landmark-npu-core"].as<std::string>()
        : default_npu_core;

    return ProgramOptionsParseResult{ProgramOptions{
        parsed_options["input-service"].as<std::string>(),
        parsed_options["output-service"].as<std::string>(),
        parsed_options["model"].as<std::string>(),
        parsed_options["landmark-model"].as<std::string>(),
        parsed_options["confidence"].as<float>(),
        parsed_options["presence-threshold"].as<float>(),
        parsed_options["tracking-threshold"].as<float>(),
        parsed_options["nms-iou-threshold"].as<float>(),
        parsed_options["tracking-iou-match"].as<float>(),
        parsed_options["crop-expansion"].as<float>(),
        parsed_options["base-roi-expansion"].as<float>(),
        parsed_options["small-hand-expansion"].as<float>(),
        parsed_options["large-hand-expansion"].as<float>(),
        parsed_options["small-hand-threshold"].as<float>(),
        parsed_options["large-hand-threshold"].as<float>(),
        parsed_options["boundary-margin"].as<float>(),
        parsed_options["boundary-min-factor"].as<float>(),
        parsed_options["euro-min-cutoff"].as<float>(),
        parsed_options["euro-beta"].as<float>(),
        parsed_options["euro-d-cutoff"].as<float>(),
        parsed_options["handedness-threshold"].as<float>(),
        parsed_options["max-tracking-gap"].as<std::uint32_t>(),
        parsed_options["max-stale-frames"].as<std::uint32_t>(),
        parsed_options["full-frame-interval"].as<std::uint32_t>(),
        subscriber_buffer_size,
        single_hand,
        parsed_options["swap-handedness"].as<bool>(),
        parse_core_mask(palm_npu_core),
        parse_core_mask(landmark_npu_core),
        parsed_options.count("verbose") != 0,
        signlang::logging::parse_cli_options(parsed_options),
    }};
  }

} // namespace signlang::handpose_det

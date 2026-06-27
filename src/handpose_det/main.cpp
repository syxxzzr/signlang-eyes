#include "common/runtime.hpp"
#include "handpose_model.hpp"
#include "handpose_transport.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <array>
#include <chrono>
#include <thread>

auto main(int argc, char** argv) -> int {
  using signlang::handpose_det::HandPoseDetection;
  using signlang::handpose_det::HandPoseModel;
  using signlang::handpose_det::HandPoseTransport;
  using signlang::handpose_det::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting hand pose detector");
    spdlog::info("Palm detector model: {}", options.palm_detector_model_path);
    spdlog::info("Landmark model: {}", options.landmark_model_path);

    const auto hand_slots = options.single_hand ? 1U : 2U;
    HandPoseModel model{options.palm_detector_model_path, options.landmark_model_path, options, hand_slots};
    spdlog::info("Hand pose model loaded successfully");

    HandPoseTransport transport{options.input_service_name, options.output_service_name, options.subscriber_buffer_size,
                                hand_slots};

    std::uint64_t sequence_number = 0;
    std::array<HandPoseDetection, 2> detection_buffer{};
    while (!signlang::runtime::shutdown_requested()) {
      if (!transport.has_subscribers()) {
        transport.detach_upstream();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }

      transport.ensure_upstream_attached();
      if (!transport.wait_for_work()) {
        continue;
      }

      transport.receive_latest([&](const signlang::handpose_det::VideoSampleView& sample) {
        auto detections = iox2::bb::MutableSlice<HandPoseDetection>{detection_buffer.data(), hand_slots};
        const auto result = model.run(*sample.metadata, sample.payload, sample.payload_size, detections);
        transport.publish(*sample.metadata, sequence_number++,
                          signlang::handpose_det::HandPosePublishInfo{
                              .detection_count = result.detection_count,
                              .image_width = result.image_width,
                              .image_height = result.image_height,
                              .model_width = result.model_width,
                              .model_height = result.model_height,
                          },
                          detection_buffer.data());
      });
    }

    return 0;
  });
}

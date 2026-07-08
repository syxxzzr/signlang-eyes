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
    auto downstream_active = false;
    while (!signlang::runtime::shutdown_requested()) {
      if (!transport.has_subscribers()) {
        if (downstream_active) {
          spdlog::info("Hand pose downstream subscriber disconnected; detaching video upstream");
          downstream_active = false;
        }
        transport.detach_upstream();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      if (!downstream_active) {
        spdlog::info("Hand pose downstream subscriber connected; attaching video upstream");
        downstream_active = true;
      }

      transport.ensure_upstream_attached();
      if (!transport.wait_for_work()) {
        continue;
      }

      transport.receive_latest([&](const signlang::handpose_det::VideoSampleView& sample) {
        auto detections = iox2::bb::MutableSlice<HandPoseDetection>{detection_buffer.data(), hand_slots};
        const auto result = model.run(*sample.metadata, sample.payload, sample.payload_size, detections);
        const auto current_sequence_number = sequence_number++;
        transport.publish(*sample.metadata, current_sequence_number,
                          signlang::handpose_det::HandPosePublishInfo{
                              result.detection_count, result.image_width, result.image_height, result.model_width,
                              result.model_height},
                          detection_buffer.data());
        if (result.detection_count > 0 || current_sequence_number % 300 == 0) {
          spdlog::debug("Published hand pose frame seq={} source_seq={} detections={}", current_sequence_number,
                        sample.metadata->sequence_number, result.detection_count);
        }
      });
    }

    spdlog::info("Hand pose detector stopped after publishing {} frames", sequence_number);
    return 0;
  });
}

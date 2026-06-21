#include "common/logging.hpp"
#include "handpose_model.hpp"
#include "handpose_transport.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <array>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <optional>
#include <thread>
#include <variant>

namespace {

  volatile std::sig_atomic_t g_should_stop = 0;

  void handle_shutdown_signal(int /* signal_number */) { g_should_stop = 1; }

  void install_signal_handlers() {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
  }

} // namespace

auto main(int argc, char** argv) -> int {
  using signlang::handpose_det::HandPoseDetection;
  using signlang::handpose_det::HandPoseModel;
  using signlang::handpose_det::HandPoseTransport;
  using signlang::handpose_det::IpcHandPoseStateMonitor;
  using signlang::handpose_det::parse_program_options;
  using signlang::handpose_det::ProgramOptions;
  using signlang::handpose_det::ProgramUsage;

  signlang::logging::initialize();

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (const auto* usage = std::get_if<ProgramUsage>(&parse_result); usage != nullptr) {
      std::cout << usage->text << '\n';
      return 0;
    }

    const auto& options = std::get<ProgramOptions>(parse_result);
    signlang::logging::initialize(options.logging);
    install_signal_handlers();

    spdlog::info("Starting hand pose detector");
    spdlog::info("Model: {}", options.model_path);

    HandPoseModel model{options.model_path, options};
    spdlog::info("Hand pose model loaded successfully");

    HandPoseTransport transport{options.input_service_name, options.output_service_name, options.subscriber_buffer_size,
                                options.max_detections};
    auto state_monitor = std::optional<IpcHandPoseStateMonitor>{};
    if (options.state_event_service_name.has_value() && options.state_blackboard_service_name.has_value()) {
      state_monitor.emplace(options.state_event_service_name.value(), options.state_blackboard_service_name.value());
    }
    auto gate_enabled = [&]() { return !state_monitor.has_value() || state_monitor->is_enabled(); };
    auto poll_gate = [&]() {
      if (state_monitor.has_value()) {
        state_monitor->try_wait_for_state_change();
      }
    };

    std::uint64_t sequence_number = 0;
    std::array<HandPoseDetection, signlang::handpose_det::kMaxHandPoseDetections> detection_buffer{};
    while (g_should_stop == 0 && transport.wait_for_work()) {
      poll_gate();

      if (!gate_enabled()) {
        // Poll for state change with stop check to avoid hang on shutdown
        while (g_should_stop == 0 && !gate_enabled()) {
          poll_gate();
          if (!gate_enabled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
        }
        if (g_should_stop != 0) {
          break;
        }
        continue;
      }

      transport.receive_latest([&](const signlang::handpose_det::VideoSampleView& sample) {
        auto detections = iox2::bb::MutableSlice<HandPoseDetection>{detection_buffer.data(), options.max_detections};
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
  } catch (const std::exception& error) {
    spdlog::error("{}", error.what());
    return 1;
  }
}

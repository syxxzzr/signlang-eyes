#include "bluetooth_gatt_server.hpp"
#include "common/runtime.hpp"
#include "common/time.hpp"
#include "iceoryx_gateway.hpp"
#include "manager_service.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <optional>

auto main(int argc, char** argv) -> int {
  using signlang::signlang_det::SignlangResult;
  using signlang::signlang_manager::BluetoothGattOptions;
  using signlang::signlang_manager::BluetoothGattServer;
  using signlang::signlang_manager::IpcHandposeSubscriber;
  using signlang::signlang_manager::IpcSignlangResultSubscriber;
  using signlang::signlang_manager::ManagerService;
  using signlang::signlang_manager::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [&](const auto& options) {
    spdlog::info("Starting sign language manager");

    auto manager = ManagerService{options};
    auto bluetooth =
        BluetoothGattServer{BluetoothGattOptions{options.adapter_path, options.bluetooth_name, options.max_notify_payload}};
    bluetooth.start([&manager](const auto& request) { return manager.handle_packet_bytes(request); });

    auto subscriber = std::optional<IpcHandposeSubscriber>{};
    auto signlang_subscriber = std::optional<IpcSignlangResultSubscriber>{};
    auto next_stream_time_ns = std::uint64_t{0};
    auto pending_signlang_result = std::optional<SignlangResult>{};
    auto stream_was_active = false;
    auto notified_packet_count = std::uint64_t{0};

    while (!signlang::runtime::shutdown_requested()) {
      const auto stream_active = manager.streaming_enabled() && bluetooth.notifications_enabled();
      if (!stream_active) {
        if (stream_was_active) {
          spdlog::info("Sign language manager stream inactive; detaching IPC subscribers and clearing pending result");
          stream_was_active = false;
        }
        subscriber.reset();
        signlang_subscriber.reset();
        pending_signlang_result.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      if (!stream_was_active) {
        spdlog::info("Sign language manager stream active; attaching IPC subscribers");
        stream_was_active = true;
      }

      if (!subscriber.has_value()) {
        spdlog::info("Sign language manager attaching handpose subscriber to {}", options.input_service_name);
        subscriber.emplace(options.input_service_name, options.subscriber_buffer_size);
      }
      if (!signlang_subscriber.has_value()) {
        spdlog::info("Sign language manager attaching signlang result subscriber to {}",
                     options.signlang_result_service_name);
        signlang_subscriber.emplace(options.signlang_result_service_name, options.subscriber_buffer_size);
      }

      signlang_subscriber->receive_latest([&](const auto& result) {
        if (result.recognized) {
          pending_signlang_result = result;
          spdlog::info("Sign language manager queued recognized result seq={} gesture={} confidence={:.2f}",
                       result.sequence_number, result.gesture_name.data(), result.confidence);
        }
      });

      if (!subscriber->wait_for_work()) {
        continue;
      }

      subscriber->receive_latest([&](const auto& metadata, const auto* detections, auto count) {
        const auto now_ns = signlang::common::steady_timestamp_ns();
        if (now_ns < next_stream_time_ns) {
          return;
        }

        const auto* signlang_result = pending_signlang_result.has_value() ? &pending_signlang_result.value() : nullptr;
        const auto packet = manager.build_stream_packet(metadata, detections, count, signlang_result);
        bluetooth.notify_packet(packet);
        ++notified_packet_count;
        if (signlang_result != nullptr || notified_packet_count % 200 == 0) {
          spdlog::info("Sent BLE stream packet count={} handpose_seq={} detections={} signlang_attached={} bytes={}",
                       notified_packet_count, metadata.sequence_number, count, signlang_result != nullptr,
                       packet.size());
        }
        pending_signlang_result.reset();
        next_stream_time_ns = now_ns + manager.stream_interval_ns();
      });
    }

    bluetooth.stop();
    spdlog::info("Sign language manager stopped after sending {} stream packets", notified_packet_count);
    return 0;
  });
}

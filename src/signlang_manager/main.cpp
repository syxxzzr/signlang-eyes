#include "bluetooth_gatt_server.hpp"
#include "common/runtime.hpp"
#include "iceoryx_gateway.hpp"
#include "manager_service.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <optional>

namespace {

  auto steady_timestamp_ns() -> std::uint64_t {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  }

} // namespace

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
    spdlog::info("Prototype database: {}", options.prototypes_path);

    auto manager = ManagerService{options};
    auto bluetooth = BluetoothGattServer{BluetoothGattOptions{
        .adapter_path = options.adapter_path,
        .local_name = options.bluetooth_name,
        .max_notify_payload = options.max_notify_payload,
    }};
    bluetooth.start([&manager](const auto& request) { return manager.handle_packet_bytes(request); });

    auto subscriber = std::optional<IpcHandposeSubscriber>{};
    auto signlang_subscriber = std::optional<IpcSignlangResultSubscriber>{};
    auto next_stream_time_ns = std::uint64_t{0};
    auto pending_signlang_result = std::optional<SignlangResult>{};

    while (!signlang::runtime::shutdown_requested()) {
      const auto stream_active = manager.streaming_enabled() && bluetooth.notifications_enabled();
      if (!stream_active) {
        subscriber.reset();
        signlang_subscriber.reset();
        pending_signlang_result.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }

      if (!subscriber.has_value()) {
        subscriber.emplace(options.input_service_name, options.subscriber_buffer_size);
      }
      if (!signlang_subscriber.has_value()) {
        signlang_subscriber.emplace(options.signlang_result_service_name, options.subscriber_buffer_size);
      }

      signlang_subscriber->receive_latest([&](const auto& result) {
        if (result.recognized) {
          pending_signlang_result = result;
        }
      });

      if (!subscriber->wait_for_work()) {
        continue;
      }

      subscriber->receive_latest([&](const auto& metadata, const auto* detections, auto count) {
        const auto now_ns = steady_timestamp_ns();
        if (now_ns < next_stream_time_ns) {
          return;
        }

        const auto* signlang_result = pending_signlang_result.has_value() ? &pending_signlang_result.value() : nullptr;
        const auto packet = manager.build_stream_packet(metadata, detections, count, signlang_result);
        bluetooth.notify_packet(packet);
        pending_signlang_result.reset();
        next_stream_time_ns = now_ns + manager.stream_interval_ns();
      });
    }

    bluetooth.stop();
    return 0;
  });
}

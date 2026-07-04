#include "common/runtime.hpp"
#include "event_listener.hpp"
#include "payload_queue.hpp"
#include "position.hpp"
#include "position_parser.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <boost/mqtt5.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace signlang::telemetry_service {
  namespace asio = boost::asio;
  namespace json = boost::json;
  namespace mqtt = boost::mqtt5;
  using TcpStream = asio::ip::tcp::socket;
  using MqttClient = mqtt::mqtt_client<TcpStream>;
  using TelemetryPayloadQueue = PayloadQueue<64>;

  namespace {

    auto payload_from_fix(const PositionFix& fix) -> std::string {
      json::object payload;
      payload["latitude"] = fix.latitude_deg;
      payload["longitude"] = fix.longitude_deg;
      if (fix.altitude_m.has_value()) {
        payload["altitude_m"] = *fix.altitude_m;
      }
      if (fix.speed_kph.has_value()) {
        payload["speed_kph"] = *fix.speed_kph;
      }
      if (fix.track_deg.has_value()) {
        payload["track_deg"] = *fix.track_deg;
      }
      return json::serialize(payload);
    }

    auto payload_from_alert_event(const AlertEvent& /* event */) -> std::string {
      json::object payload;
      payload["type"] = "alert";
      return json::serialize(payload);
    }

  } // namespace

  class TelemetryService : public std::enable_shared_from_this<TelemetryService> {
  public:
    TelemetryService(asio::io_context& serial_io_context, asio::io_context& mqtt_io_context, ProgramOptions options)
        : serial_io_context_{serial_io_context},
          mqtt_io_context_{mqtt_io_context},
          serial_{serial_io_context},
          mqtt_client_{mqtt_io_context},
          mqtt_drain_timer_{mqtt_io_context},
          options_{std::move(options)},
          alert_listener_{options_.alert_event_service, "telemetry_service_alert_listener",
                          [this](const AlertEvent& event) {
                            asio::post(serial_io_context_, [this, event] {
                              if (!stop_requested_.load(std::memory_order_acquire)) {
                                publish_alert(event);
                              }
                            });
                          }} {}

    void start() {
      spdlog::info("Starting telemetry service");
      serial_.open(options_.serial_device);
      serial_.set_option(asio::serial_port_base::baud_rate{options_.baud_rate});
      serial_.set_option(asio::serial_port_base::character_size{8});
      serial_.set_option(asio::serial_port_base::parity{asio::serial_port_base::parity::none});
      serial_.set_option(asio::serial_port_base::stop_bits{asio::serial_port_base::stop_bits::one});
      serial_.set_option(asio::serial_port_base::flow_control{asio::serial_port_base::flow_control::none});

      mqtt_client_.brokers(options_.mqtt_host, options_.mqtt_port)
          .credentials(options_.mqtt_client_id, options_.mqtt_username, options_.mqtt_password)
          .keep_alive(options_.keep_alive_seconds)
          .async_run(asio::detached);
      schedule_mqtt_drain();
      alert_listener_.start();

      spdlog::info("serial device: {} @ {}", options_.serial_device, options_.baud_rate);
      spdlog::info("mqtt broker: {}:{}, topic: {}", options_.mqtt_host, options_.mqtt_port, options_.mqtt_topic);
      if (!options_.alert_event_service.empty()) {
        spdlog::info("alert event service: {}, topic: {}", options_.alert_event_service, options_.alert_mqtt_topic);
      }
      read_next_line();
    }

    void stop() {
      if (stop_requested_.exchange(true, std::memory_order_acq_rel)) {
        return;
      }
      spdlog::info("Stopping telemetry service");
      boost::system::error_code ignored;
      serial_.cancel(ignored);
      serial_.close(ignored);
      alert_listener_.stop();
    }

    [[nodiscard]] auto mqtt_done() const -> bool { return mqtt_done_.load(std::memory_order_acquire); }

  private:
    void read_next_line() {
      asio::async_read_until(serial_, buffer_, '\n',
                             [self = shared_from_this()](const boost::system::error_code& error,
                                                         std::size_t bytes_transferred) {
                               self->handle_line(error, bytes_transferred);
                             });
    }

    void handle_line(const boost::system::error_code& error, std::size_t /* bytes_transferred */) {
      if (error) {
        if (!signlang::runtime::shutdown_requested()) {
          spdlog::error("serial read failed: {}", error.message());
        }
        serial_io_context_.stop();
        return;
      }

      std::istream input_stream{&buffer_};
      std::string line;
      std::getline(input_stream, line);

      auto result = parser_.parse_line(line);
      if (result.fix.has_value()) {
        publish(*result.fix);
      } else {
        log_position_parse_warning(result.status, line);
      }

      if (signlang::runtime::shutdown_requested()) {
        stop();
        serial_io_context_.stop();
        return;
      }
      read_next_line();
    }

    void log_position_parse_warning(PositionParseStatus status, const std::string& line) const {
      if (status == PositionParseStatus::Empty) {
        return;
      }

      constexpr auto kMaxLoggedSentenceLength = std::size_t{96};
      auto sentence = line.substr(0, kMaxLoggedSentenceLength);
      if (line.size() > kMaxLoggedSentenceLength) {
        sentence += "...";
      }

      switch (status) {
        case PositionParseStatus::InvalidSentence:
          spdlog::warn("failed to parse GPS serial sentence: invalid NMEA sentence '{}'", sentence);
          break;
        case PositionParseStatus::UnsupportedSentence:
          spdlog::warn("GPS serial sentence has no supported position payload: '{}'", sentence);
          break;
        case PositionParseStatus::ParseFailed:
          spdlog::warn("failed to parse GPS serial sentence payload: '{}'", sentence);
          break;
        case PositionParseStatus::NoFix:
          spdlog::warn("GPS serial sentence parsed without a valid position fix: '{}'", sentence);
          break;
        case PositionParseStatus::InvalidCoordinates:
          spdlog::warn("GPS serial sentence parsed with invalid coordinates: '{}'", sentence);
          break;
        case PositionParseStatus::Empty:
        case PositionParseStatus::ValidFix:
          break;
      }
    }

    void publish(const PositionFix& fix) {
      if (!payload_queue_.push(MqttPayload{options_.mqtt_topic, payload_from_fix(fix)})) {
        spdlog::warn("telemetry payload queue is full; dropping newest position fix");
        return;
      }

      ++position_fix_count_;
      if (position_fix_count_ % 500 == 0) {
        spdlog::info("Processed position fixes count={} latest_lat={} latest_lon={}", position_fix_count_,
                     fix.latitude_deg, fix.longitude_deg);
      }
    }

    void publish_alert(const AlertEvent& event) {
      if (!payload_queue_.push(MqttPayload{options_.alert_mqtt_topic, payload_from_alert_event(event)})) {
        spdlog::warn("telemetry payload queue is full; dropping alert event");
        return;
      }
      spdlog::info("queued alert MQTT payload from event {} for topic {}", event.id, options_.alert_mqtt_topic);
    }

    void schedule_mqtt_drain() {
      mqtt_drain_timer_.expires_after(std::chrono::milliseconds{20});
      mqtt_drain_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
        if (error) {
          return;
        }
        self->drain_mqtt_queue();
      });
    }

    void drain_mqtt_queue() {
      if (stop_requested_.load(std::memory_order_acquire)) {
        spdlog::info("draining remaining MQTT payloads before shutdown");
        while (auto payload = payload_queue_.pop()) {
          publish_payload(std::make_shared<MqttPayload>(std::move(*payload)));
        }
        mqtt_client_.cancel();
        mqtt_done_.store(true, std::memory_order_release);
        spdlog::info("telemetry service MQTT client cancelled after drain");
        return;
      }

      constexpr auto kMaxPayloadsPerTick = 32;
      for (auto i = 0; i < kMaxPayloadsPerTick; ++i) {
        auto payload = payload_queue_.pop();
        if (!payload.has_value()) {
          break;
        }
        publish_payload(std::make_shared<MqttPayload>(std::move(*payload)));
      }

      schedule_mqtt_drain();
    }

    void publish_payload(const std::shared_ptr<MqttPayload>& payload) {
      const auto retain = options_.retain ? mqtt::retain_e::yes : mqtt::retain_e::no;
      const auto on_publish = [payload](boost::system::error_code error, auto&&...) {
        if (error) {
          spdlog::warn("mqtt publish failed: {}", error.message());
          return;
        }
      };

      switch (options_.qos) {
        case 0:
          mqtt_client_.async_publish<mqtt::qos_e::at_most_once>(
              payload->topic, payload->payload, retain, mqtt::publish_props{}, on_publish);
          break;
        case 1:
          mqtt_client_.async_publish<mqtt::qos_e::at_least_once>(
              payload->topic, payload->payload, retain, mqtt::publish_props{}, on_publish);
          break;
        case 2:
          mqtt_client_.async_publish<mqtt::qos_e::exactly_once>(
              payload->topic, payload->payload, retain, mqtt::publish_props{}, on_publish);
          break;
        default:
          break;
      }
    }

    asio::io_context& serial_io_context_;
    asio::io_context& mqtt_io_context_;
    asio::serial_port serial_;
    asio::streambuf buffer_;
    MqttClient mqtt_client_;
    asio::steady_timer mqtt_drain_timer_;
    NmeaPositionParser parser_;
    TelemetryPayloadQueue payload_queue_;
    ProgramOptions options_;
    EventListener alert_listener_;
    std::uint64_t position_fix_count_{0};
    std::atomic_bool stop_requested_{false};
    std::atomic_bool mqtt_done_{false};
  };

} // namespace signlang::telemetry_service

auto main(int argc, char** argv) -> int {
  using signlang::telemetry_service::TelemetryService;
  using signlang::telemetry_service::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    boost::asio::io_context serial_io_context;
    boost::asio::io_context mqtt_io_context;
    auto mqtt_work = boost::asio::make_work_guard(mqtt_io_context);
    auto service = std::make_shared<TelemetryService>(serial_io_context, mqtt_io_context, options);
    service->start();
    auto mqtt_thread = std::thread{[&] { mqtt_io_context.run(); }};

    boost::asio::steady_timer shutdown_timer{serial_io_context};
    std::function<void()> poll_shutdown;
    poll_shutdown = [&] {
      shutdown_timer.expires_after(std::chrono::milliseconds{200});
      shutdown_timer.async_wait([&](const boost::system::error_code& error) {
        if (error) {
          return;
        }
        if (signlang::runtime::shutdown_requested()) {
          service->stop();
          serial_io_context.stop();
          return;
        }
        poll_shutdown();
      });
    };
    poll_shutdown();

    serial_io_context.run();
    service->stop();
    while (!service->mqtt_done()) {
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    mqtt_work.reset();
    if (mqtt_thread.joinable()) {
      mqtt_thread.join();
    }
    return 0;
  });
}

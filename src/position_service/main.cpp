#include "common/runtime.hpp"
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

namespace signlang::position_service {
  namespace asio = boost::asio;
  namespace json = boost::json;
  namespace mqtt = boost::mqtt5;
  using TcpStream = asio::ip::tcp::socket;
  using MqttClient = mqtt::mqtt_client<TcpStream>;
  using PositionPayloadQueue = PayloadQueue<64>;

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
      if (fix.satellites.has_value()) {
        payload["satellites"] = *fix.satellites;
      }
      if (fix.hdop.has_value()) {
        payload["hdop"] = *fix.hdop;
      }
      payload["source"] = fix.source_sentence;
      return json::serialize(payload);
    }

  } // namespace

  class PositionService : public std::enable_shared_from_this<PositionService> {
  public:
    PositionService(asio::io_context& serial_io_context, asio::io_context& mqtt_io_context, ProgramOptions options)
        : serial_io_context_{serial_io_context},
          mqtt_io_context_{mqtt_io_context},
          serial_{serial_io_context},
          mqtt_client_{mqtt_io_context},
          mqtt_drain_timer_{mqtt_io_context},
          options_{std::move(options)} {}

    void start() {
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

      spdlog::info("serial device: {} @ {}", options_.serial_device, options_.baud_rate);
      spdlog::info("mqtt broker: {}:{}, topic: {}", options_.mqtt_host, options_.mqtt_port, options_.mqtt_topic);
      read_next_line();
    }

    void stop() {
      stop_requested_.store(true, std::memory_order_release);
      boost::system::error_code ignored;
      serial_.cancel(ignored);
      serial_.close(ignored);
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

      if (auto fix = parser_.parse_line(line)) {
        publish(*fix);
      }

      if (signlang::runtime::shutdown_requested()) {
        stop();
        serial_io_context_.stop();
        return;
      }
      read_next_line();
    }

    void publish(const PositionFix& fix) {
      if (!payload_queue_.push(payload_from_fix(fix))) {
        spdlog::warn("position payload queue is full; dropping newest fix");
      }
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
        while (auto payload = payload_queue_.pop()) {
          publish_payload(std::make_shared<std::string>(std::move(*payload)));
        }
        mqtt_client_.cancel();
        mqtt_done_.store(true, std::memory_order_release);
        return;
      }

      constexpr auto kMaxPayloadsPerTick = 32;
      for (auto i = 0; i < kMaxPayloadsPerTick; ++i) {
        auto payload = payload_queue_.pop();
        if (!payload.has_value()) {
          break;
        }
        publish_payload(std::make_shared<std::string>(std::move(*payload)));
      }

      schedule_mqtt_drain();
    }

    void publish_payload(const std::shared_ptr<std::string>& payload) {
      const auto retain = options_.retain ? mqtt::retain_e::yes : mqtt::retain_e::no;
      const auto on_publish = [payload](boost::system::error_code error, auto&&...) {
        if (error) {
          spdlog::warn("mqtt publish failed: {}", error.message());
          return;
        }
        spdlog::info("published position payload ({} bytes)", payload->size());
      };

      switch (options_.qos) {
        case 0:
          mqtt_client_.async_publish<mqtt::qos_e::at_most_once>(
              options_.mqtt_topic, *payload, retain, mqtt::publish_props{}, on_publish);
          break;
        case 1:
          mqtt_client_.async_publish<mqtt::qos_e::at_least_once>(
              options_.mqtt_topic, *payload, retain, mqtt::publish_props{}, on_publish);
          break;
        case 2:
          mqtt_client_.async_publish<mqtt::qos_e::exactly_once>(
              options_.mqtt_topic, *payload, retain, mqtt::publish_props{}, on_publish);
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
    PositionPayloadQueue payload_queue_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool mqtt_done_{false};
    ProgramOptions options_;
  };

} // namespace signlang::position_service

auto main(int argc, char** argv) -> int {
  using signlang::position_service::PositionService;
  using signlang::position_service::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    boost::asio::io_context serial_io_context;
    boost::asio::io_context mqtt_io_context;
    auto mqtt_work = boost::asio::make_work_guard(mqtt_io_context);
    auto service = std::make_shared<PositionService>(serial_io_context, mqtt_io_context, options);
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

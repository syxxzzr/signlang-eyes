#include "common/fixed_string.hpp"
#include "common/runtime.hpp"
#include "hex_font.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "scrolling_display.hpp"
#include "serial_protocol.hpp"
#include "serial_transport.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace signlang::peripheral_service {
  namespace {

    [[nodiscard]] auto ok_response(std::uint32_t request_id, const std::string& message) -> DisplayResponse {
      auto response = DisplayResponse{request_id, DisplayStatus::Ok, {}};
      signlang::common::copy_fixed_string(message, response.message);
      return response;
    }

    [[nodiscard]] auto bad_request_response(std::uint32_t request_id, const std::string& message) -> DisplayResponse {
      auto response = DisplayResponse{request_id, DisplayStatus::BadRequest, {}};
      signlang::common::copy_fixed_string(message, response.message);
      return response;
    }

    class DisplayWorker {
    public:
      DisplayWorker(const HexFont& font, DisplayOptions options, SerialTransport& serial) :
          display_{font, options}, refresh_interval_{options.refresh_interval_ms}, serial_{serial} {}

      ~DisplayWorker() { stop(); }

      DisplayWorker(const DisplayWorker&) = delete;
      auto operator=(const DisplayWorker&) -> DisplayWorker& = delete;

      void start() {
        if (running_) {
          return;
        }
        stop_requested_ = false;
        running_ = true;
        thread_ = std::thread{&DisplayWorker::run, this};
        spdlog::info("peripheral display worker started");
      }

      void stop() {
        {
          std::lock_guard lock{mutex_};
          if (!running_ && !thread_.joinable()) {
            return;
          }
          stop_requested_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) {
          thread_.join();
        }
        running_ = false;
        spdlog::info("peripheral display worker stopped");
      }

      void set_title_line(std::string text) { push(Command{CommandType::SetTitleLine, std::move(text)}); }

      void set_content_line(std::string text) { push(Command{CommandType::SetContentLine, std::move(text)}); }

      void clear_content_line() { push(Command{CommandType::ClearContentLine, {}}); }

    private:
      enum class CommandType {
        SetTitleLine,
        SetContentLine,
        ClearContentLine,
      };

      struct Command {
        CommandType type;
        std::string text;
      };

      void push(Command command) {
        {
          std::lock_guard lock{mutex_};
          commands_.push_back(std::move(command));
        }
        cv_.notify_all();
      }

      void run() {
        spdlog::info("peripheral display worker sending full refresh");
        serial_.async_send_many(display_.full_refresh());

        auto next_tick = ScrollingDisplay::Clock::now();
        while (true) {
          auto commands = std::deque<Command>{};
          {
            std::unique_lock lock{mutex_};
            cv_.wait_until(lock, next_tick, [this] { return stop_requested_ || !commands_.empty(); });
            if (stop_requested_) {
              break;
            }
            commands.swap(commands_);
          }

          for (auto& command : commands) {
            switch (command.type) {
            case CommandType::SetTitleLine:
              spdlog::info("peripheral display worker applying title line ({} chars)", command.text.size());
              display_.set_first_line(std::move(command.text));
              break;
            case CommandType::SetContentLine:
              spdlog::info("peripheral display worker applying content line ({} chars)", command.text.size());
              display_.set_second_line(std::move(command.text));
              break;
            case CommandType::ClearContentLine:
              spdlog::info("peripheral display worker clearing content line");
              display_.clear_second_line();
              break;
            }
          }

          const auto now = ScrollingDisplay::Clock::now();
          auto frames = display_.tick(now);
          if (!frames.empty()) {
            serial_.async_send_many(std::move(frames));
          }
          next_tick = now + std::chrono::milliseconds{refresh_interval_};
        }
      }

      ScrollingDisplay display_;
      std::uint32_t refresh_interval_;
      SerialTransport& serial_;
      std::mutex mutex_;
      std::condition_variable cv_;
      std::deque<Command> commands_;
      std::thread thread_;
      bool running_{false};
      bool stop_requested_{false};
    };

  } // namespace

  class PeripheralService {
  public:
    explicit PeripheralService(ProgramOptions options) :
        options_{std::move(options)},
        font_{options_.font_file},
        state_control_client_{options_.state_control_service_name},
        alert_notifier_{options_.alert_event_service_name},
        serial_{options_.serial,
                [this](ButtonEvent event) {
                  handle_button_event(event);
                }},
        display_worker_{font_, options_.display, serial_},
        display_server_{options_.display_service_name} {}

    void run() {
      spdlog::info("Starting peripheral service");
      serial_.start();
      display_worker_.start();
      spdlog::info("peripheral serial device: {} @ {}", options_.serial.device, options_.serial.baud_rate);
      spdlog::info("peripheral display service: {}", options_.display_service_name);
      spdlog::info("peripheral state control service: {}", options_.state_control_service_name);
      spdlog::info("peripheral alert event service: {}", options_.alert_event_service_name);

      while (!signlang::runtime::shutdown_requested()) {
        (void)display_server_.wait_for_work(100);
        display_server_.process_pending_requests([this](const DisplayRequest& request) {
          return handle_display_request(request);
        });
      }

      display_worker_.stop();
      serial_.async_send(make_motor_frame(false));
      serial_.stop();
      spdlog::info("Peripheral service stopped");
    }

  private:
    auto handle_display_request(const DisplayRequest& request) -> DisplayResponse {
      spdlog::info("peripheral display IPC request {} command {}", request.request_id,
                   static_cast<std::uint32_t>(request.command));
      switch (request.command) {
      case DisplayCommand::SetTitleLine:
        display_worker_.set_title_line(signlang::common::fixed_string_to_string(request.text));
        return ok_response(request.request_id, "updated");
      case DisplayCommand::SetContentLine:
        display_worker_.set_content_line(signlang::common::fixed_string_to_string(request.text));
        return ok_response(request.request_id, "updated");
      case DisplayCommand::ClearContentLine:
        display_worker_.clear_content_line();
        return ok_response(request.request_id, "cleared");
      }
      return bad_request_response(request.request_id, "invalid display command");
    }

    void handle_button_event(ButtonEvent event) {
      switch (event) {
      case ButtonEvent::SingleClick:
        spdlog::info("peripheral button single-click received");
        if (!state_control_client_.has_server()) {
          spdlog::warn("state control server is not available for button single-click");
          return;
        }
        state_control_client_.request_next_base_state();
        spdlog::info("requested next base app state through IPC");
        break;
      case ButtonEvent::DoubleClick:
        spdlog::info("peripheral button double-click received; notifying alert event");
        alert_notifier_.notify_alert();
        break;
      }
    }

    ProgramOptions options_;
    HexFont font_;
    IpcStateControlClient state_control_client_;
    IpcAlertNotifier alert_notifier_;
    SerialTransport serial_;
    DisplayWorker display_worker_;
    IpcDisplayServer display_server_;
  };

} // namespace signlang::peripheral_service

auto main(int argc, char** argv) -> int {
  using signlang::peripheral_service::PeripheralService;
  using signlang::peripheral_service::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    auto service = PeripheralService{options};
    service.run();
    return 0;
  });
}

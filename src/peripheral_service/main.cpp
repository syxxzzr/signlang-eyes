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

    [[nodiscard]] auto state_chinese_name(signlang::state_machine::AppState state) -> const char* {
      switch (state) {
      case signlang::state_machine::AppState::Normal:
        return "普通";
      case signlang::state_machine::AppState::Asr:
        return "语音识别";
      case signlang::state_machine::AppState::SignLanguageChat:
        return "手语聊天";
      case signlang::state_machine::AppState::SignLanguageAi:
        return "手语AI";
      case signlang::state_machine::AppState::DangerousSound:
        return "危险声音";
      }
      return "未知";
    }

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
      }

      void stop() {
        {
          std::lock_guard lock{mutex_};
          stop_requested_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) {
          thread_.join();
        }
        running_ = false;
      }

      void set_first_line(std::string text) { push(Command{CommandType::SetFirstLine, std::move(text)}); }

      void set_second_line(std::string text) { push(Command{CommandType::SetSecondLine, std::move(text)}); }

      void clear_second_line() { push(Command{CommandType::ClearSecondLine, {}}); }

    private:
      enum class CommandType {
        SetFirstLine,
        SetSecondLine,
        ClearSecondLine,
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
            case CommandType::SetFirstLine:
              display_.set_first_line(std::move(command.text));
              break;
            case CommandType::SetSecondLine:
              display_.set_second_line(std::move(command.text));
              break;
            case CommandType::ClearSecondLine:
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
        display_server_{options_.display_service_name},
        state_watcher_{options_.state_event_service_name, options_.state_blackboard_service_name,
                       [this](signlang::state_machine::AppState state) {
                         handle_state_change(state);
                       }} {}

    void run() {
      serial_.start();
      display_worker_.start();
      state_watcher_.start();
      spdlog::info("peripheral serial device: {} @ {}", options_.serial.device, options_.serial.baud_rate);
      spdlog::info("peripheral display service: {}", options_.display_service_name);

      while (!signlang::runtime::shutdown_requested()) {
        (void)display_server_.wait_for_work(100);
        display_server_.process_pending_requests([this](const DisplayRequest& request) {
          return handle_display_request(request);
        });
      }

      state_watcher_.stop();
      display_worker_.stop();
      serial_.async_send(make_motor_frame(false));
      serial_.stop();
    }

  private:
    auto handle_display_request(const DisplayRequest& request) -> DisplayResponse {
      switch (request.command) {
      case DisplayCommand::SetSecondLine:
        display_worker_.set_second_line(signlang::common::fixed_string_to_string(request.text));
        return ok_response(request.request_id, "updated");
      case DisplayCommand::ClearSecondLine:
        display_worker_.clear_second_line();
        return ok_response(request.request_id, "cleared");
      }
      return bad_request_response(request.request_id, "invalid display command");
    }

    void handle_button_event(ButtonEvent event) {
      switch (event) {
      case ButtonEvent::SingleClick:
        if (!state_control_client_.has_server()) {
          spdlog::warn("state control server is not available for button single-click");
          return;
        }
        state_control_client_.request_next_base_state();
        break;
      case ButtonEvent::DoubleClick:
        alert_notifier_.notify_alert();
        break;
      }
    }

    void handle_state_change(signlang::state_machine::AppState state) {
      spdlog::info("peripheral observed state: {}", state_chinese_name(state));
      display_worker_.set_first_line(state_chinese_name(state));
      display_worker_.clear_second_line();
      serial_.async_send(make_motor_frame(state == signlang::state_machine::AppState::DangerousSound));
    }

    ProgramOptions options_;
    HexFont font_;
    IpcStateControlClient state_control_client_;
    IpcAlertNotifier alert_notifier_;
    SerialTransport serial_;
    DisplayWorker display_worker_;
    IpcDisplayServer display_server_;
    IpcStateWatcher state_watcher_;
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

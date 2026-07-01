#include "common/fixed_string.hpp"
#include "common/runtime.hpp"
#include "iceoryx_gateway.hpp"
#include "llm_protocol.hpp"
#include "openai_client.hpp"
#include "program_options.hpp"
#include "spdlog/spdlog.h"

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace signlang::llm_client {
  namespace {

    auto read_text_file(const std::string& path) -> std::string {
      std::ifstream input{path};
      if (!input) {
        throw std::runtime_error("Failed to open LLM system prompt file: " + path);
      }

      return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    auto make_response(const LlmRequest& request, LlmResponseStatus status, std::uint32_t http_status,
                       const std::string& text, const std::string& error_message) -> LlmResponse {
      auto response = LlmResponse{};
      response.status = status;
      response.request_id = request.request_id;
      response.http_status = http_status;
      common::copy_fixed_string(text, response.text);
      common::copy_fixed_string(error_message, response.error_message);
      return response;
    }

    auto response_from_completion(const LlmRequest& request, const LlmCompletionResult& completion) -> LlmResponse {
      if (!completion.error_message.empty()) {
        return make_response(request, LlmResponseStatus::ApiError, completion.http_status, {}, completion.error_message);
      }

      if (completion.text.size() >= kMaxResponseTextLength) {
        return make_response(request, LlmResponseStatus::ResponseTooLarge, completion.http_status, {},
                             "LLM response text exceeds IPC response capacity");
      }

      return make_response(request, LlmResponseStatus::Ok, completion.http_status, completion.text, {});
    }

    auto send_response(IpcLlmServer::ActiveLlmRequest& active_request, const LlmResponse& response) -> bool {
      const auto send_result = active_request.send_copy(response);
      if (!send_result.has_value()) {
        return false;
      }
      return true;
    }

    template <typename T>
    class SpscRingQueue {
    public:
      explicit SpscRingQueue(std::uint32_t capacity) : slots_(static_cast<std::size_t>(capacity) + 1U) {}

      auto push(T&& value) -> bool {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next_head = increment(head);
        if (next_head == tail_.load(std::memory_order_acquire)) {
          return false;
        }

        slots_[head].emplace(std::move(value));
        head_.store(next_head, std::memory_order_release);
        return true;
      }

      auto pop() -> std::optional<T> {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
          return std::nullopt;
        }

        auto value = std::move(slots_[tail].value());
        slots_[tail].reset();
        tail_.store(increment(tail), std::memory_order_release);
        return value;
      }

      [[nodiscard]] auto empty() const -> bool {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
      }

    private:
      [[nodiscard]] auto increment(std::size_t index) const -> std::size_t { return (index + 1U) % slots_.size(); }

      std::vector<std::optional<T>> slots_;
      alignas(64) std::atomic_size_t head_{0};
      alignas(64) std::atomic_size_t tail_{0};
    };

    struct LlmWorkItem {
      IpcLlmServer::ActiveLlmRequest active_request;
      LlmRequest request;
      std::string prompt;
    };

    class LlmWorker {
    public:
      LlmWorker(std::uint32_t worker_id, const ProgramOptions& options, const std::string& builtin_prompt,
                std::uint32_t queue_capacity) :
          worker_id_{worker_id}, queue_{queue_capacity}, client_{io_context_, options, builtin_prompt},
          thread_{[this] { run(); }} {}

      ~LlmWorker() {
        stop_requested_.store(true, std::memory_order_release);
        if (thread_.joinable()) {
          thread_.join();
        }
      }

      LlmWorker(const LlmWorker&) = delete;
      auto operator=(const LlmWorker&) -> LlmWorker& = delete;
      LlmWorker(LlmWorker&&) = delete;
      auto operator=(LlmWorker&&) -> LlmWorker& = delete;

      auto submit(LlmWorkItem& work_item) -> bool { return queue_.push(std::move(work_item)); }

    private:
      void run() {
        while (!stop_requested_.load(std::memory_order_acquire) || !queue_.empty()) {
          auto work_item = queue_.pop();
          if (!work_item.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            continue;
          }

          handle_work_item(std::move(work_item.value()));
        }
      }

      void handle_work_item(LlmWorkItem work_item) {
        try {
          const auto completion = client_.complete(work_item.prompt);
          const auto response = response_from_completion(work_item.request, completion);
          if (!send_response(work_item.active_request, response)) {
            spdlog::warn("LLM worker {} failed to send response for request {}", worker_id_,
                         work_item.request.request_id);
          }
        } catch (const std::exception& error) {
          spdlog::warn("LLM worker {} request {} failed: {}", worker_id_, work_item.request.request_id, error.what());
          const auto response =
              make_response(work_item.request, LlmResponseStatus::NetworkError, 0, {}, error.what());
          if (!send_response(work_item.active_request, response)) {
            spdlog::warn("LLM worker {} failed to send error response for request {}", worker_id_,
                         work_item.request.request_id);
          }
        }
      }

      std::uint32_t worker_id_;
      SpscRingQueue<LlmWorkItem> queue_;
      boost::asio::io_context io_context_;
      OpenAiClient client_;
      std::atomic_bool stop_requested_{false};
      std::thread thread_;
    };

    class LlmWorkerPool {
    public:
      LlmWorkerPool(const ProgramOptions& options, const std::string& builtin_prompt) {
        workers_.reserve(options.concurrency);
        for (auto worker_id = std::uint32_t{0}; worker_id < options.concurrency; ++worker_id) {
          workers_.push_back(std::make_unique<LlmWorker>(worker_id, options, builtin_prompt,
                                                         options.worker_queue_capacity));
        }
      }

      auto submit(LlmWorkItem& work_item) -> bool {
        const auto worker_count = workers_.size();
        for (auto attempt = std::size_t{0}; attempt < worker_count; ++attempt) {
          const auto index = next_worker_.fetch_add(1, std::memory_order_relaxed) % worker_count;
          if (workers_[index]->submit(work_item)) {
            return true;
          }
        }
        return false;
      }

    private:
      std::vector<std::unique_ptr<LlmWorker>> workers_;
      std::atomic_size_t next_worker_{0};
    };

  } // namespace
} // namespace signlang::llm_client

auto main(int argc, char** argv) -> int {
  using signlang::llm_client::IpcLlmServer;
  using signlang::llm_client::LlmResponseStatus;
  using signlang::llm_client::OpenAiClient;
  using signlang::llm_client::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    spdlog::info("Starting LLM client");
    spdlog::info("Service: {}", options.service_name);
    spdlog::info("Base URL: {}", options.base_url);
    spdlog::info("Model: {}", options.model);
    spdlog::info("System prompt file: {}", options.system_prompt_file);
    spdlog::info("Concurrency: {}, worker queue capacity: {}", options.concurrency, options.worker_queue_capacity);

    auto builtin_prompt = signlang::llm_client::read_text_file(options.system_prompt_file);
    spdlog::info("Loaded builtin prompt ({} bytes)", builtin_prompt.size());

    signlang::llm_client::LlmWorkerPool worker_pool{options, builtin_prompt};
    IpcLlmServer server{options.service_name};

    while (!signlang::runtime::shutdown_requested()) {
      if (!server.wait_for_work(50)) {
        continue;
      }

      server.process_pending_requests([&](IpcLlmServer::ActiveLlmRequest active_request) {
        const auto request = active_request.payload();
        const auto prompt = signlang::common::fixed_string_to_string(request.prompt);
        if (prompt.empty()) {
          const auto response = signlang::llm_client::make_response(request, LlmResponseStatus::BadRequest, 0, {},
                                                                    "LLM prompt must not be empty");
          if (!signlang::llm_client::send_response(active_request, response)) {
            spdlog::warn("Failed to send bad request response for LLM request {}", request.request_id);
          }
          return;
        }

        auto work_item = signlang::llm_client::LlmWorkItem{std::move(active_request), request, prompt};
        if (!worker_pool.submit(work_item)) {
          const auto response = signlang::llm_client::make_response(
              request, LlmResponseStatus::NetworkError, 0, {}, "LLM worker queue is full");
          if (!signlang::llm_client::send_response(work_item.active_request, response)) {
            spdlog::warn("Failed to send busy response for LLM request {}", request.request_id);
          }
        }
      });
    }

    return 0;
  });
}

#include "speech_tts_service.hpp"

#include "common/fixed_string.hpp"

#include <utility>

namespace signlang::speech_tts {

  void copy_request_text(const std::string& text, SpeechTtsRequest& request) {
    common::copy_fixed_string(text, request.text);
  }

  auto SpeechTtsService::accept(const SpeechTtsRequest& request) -> SpeechTtsResponse {
    auto response = SpeechTtsResponse{};
    response.request_id = request.request_id;

    auto text = common::fixed_string_to_string(request.text);
    if (text.empty()) {
      response.status = SpeechTtsStatus::BadRequest;
      common::copy_fixed_string("empty text", response.message);
      return response;
    }

    {
      const std::lock_guard lock{mutex_};
      ++generation_;
      response.generation = generation_;
      pending_task_ = SpeechTtsTask{generation_, std::move(text)};
      response.status = SpeechTtsStatus::Ok;
      common::copy_fixed_string("accepted", response.message);
    }

    task_available_.notify_one();
    return response;
  }

  auto SpeechTtsService::wait_for_next_task(std::chrono::milliseconds timeout) -> std::optional<SpeechTtsTask> {
    auto lock = std::unique_lock{mutex_};
    const auto ready = task_available_.wait_for(lock, timeout, [&] { return stopped_ || pending_task_.has_value(); });
    if (!ready || stopped_) {
      return std::nullopt;
    }

    auto task = std::move(pending_task_);
    pending_task_.reset();
    return task;
  }

  auto SpeechTtsService::should_cancel(std::uint64_t generation) const -> bool {
    const std::lock_guard lock{mutex_};
    return stopped_ || generation != generation_;
  }

  void SpeechTtsService::stop() {
    {
      const std::lock_guard lock{mutex_};
      stopped_ = true;
      pending_task_.reset();
    }
    task_available_.notify_all();
  }

} // namespace signlang::speech_tts

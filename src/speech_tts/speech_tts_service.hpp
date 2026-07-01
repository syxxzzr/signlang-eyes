#ifndef SIGNLANG_EYES_SPEECH_TTS_SERVICE_HPP
#define SIGNLANG_EYES_SPEECH_TTS_SERVICE_HPP

#include "speech_tts_protocol.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace signlang::speech_tts {

  struct SpeechTtsTask {
    std::uint64_t generation;
    std::string text;
  };

  void copy_request_text(const std::string& text, SpeechTtsRequest& request);

  class SpeechTtsService {
  public:
    [[nodiscard]] auto accept(const SpeechTtsRequest& request) -> SpeechTtsResponse;
    [[nodiscard]] auto wait_for_next_task(std::chrono::milliseconds timeout) -> std::optional<SpeechTtsTask>;
    [[nodiscard]] auto should_cancel(std::uint64_t generation) const -> bool;
    void stop();

  private:
    mutable std::mutex mutex_;
    std::condition_variable task_available_;
    std::optional<SpeechTtsTask> pending_task_;
    std::uint64_t generation_{0};
    bool stopped_{false};
  };

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_SERVICE_HPP

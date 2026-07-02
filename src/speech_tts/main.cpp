#include "alsa_playback_device.hpp"
#include "common/fixed_string.hpp"
#include "common/runtime.hpp"
#include "iceoryx_gateway.hpp"
#include "piper_synthesizer.hpp"
#include "program_options.hpp"
#include "speech_tts_service.hpp"
#include "spdlog/spdlog.h"

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace signlang::speech_tts {
  namespace {

    void playback_loop(const ProgramOptions& options, SpeechTtsService& service) {
      auto synthesizer = PiperSynthesizer{options};
      auto playback = std::unique_ptr<AlsaPlaybackDevice>{};

      while (!runtime::shutdown_requested()) {
        auto task = service.wait_for_next_task(std::chrono::milliseconds{50});
        if (!task.has_value()) {
          continue;
        }

        spdlog::info("Speaking request generation {}: {}", task->generation, task->text);
        try {
          const auto should_cancel = [&] {
            return service.should_cancel(task->generation) || runtime::shutdown_requested();
          };
          synthesizer.synthesize(task->text, should_cancel, [&](const PiperAudioChunkView& chunk) {
            if (should_cancel()) {
              if (playback) {
                playback->cancel();
              }
              return false;
            }

            if (chunk.sample_count == 0) {
              return true;
            }

            if (!playback || playback->sample_rate_hz() != chunk.sample_rate_hz ||
                playback->device_name() != options.audio_device_name) {
              playback = std::make_unique<AlsaPlaybackDevice>(options.audio_device_name, chunk.sample_rate_hz);
            }

            playback->play(chunk.samples, chunk.sample_count, should_cancel);
            return !should_cancel();
          });
        } catch (const std::exception& error) {
          spdlog::error("Speech TTS playback failed: {}", error.what());
        }
      }
    }

  } // namespace
} // namespace signlang::speech_tts

auto main(int argc, char** argv) -> int {
  using signlang::speech_tts::IpcSpeechTtsServer;
  using signlang::speech_tts::SpeechTtsService;
  using signlang::speech_tts::parse_program_options;

  return signlang::runtime::run_module(argc, argv, parse_program_options, [](const auto& options) {
    spdlog::info("Starting speech TTS");
    spdlog::info("Service: {}", options.service_name);
    spdlog::info("ALSA playback device: {}", options.audio_device_name);
    spdlog::info("Piper encoder model: {}", options.encoder_model_path);
    spdlog::info("Piper decoder model: {}", options.decoder_model_path);
    spdlog::info("Piper config: {}", options.config_path);
    spdlog::info("cpp-pinyin dictionary: {}", options.pinyin_dictionary_path);

    auto service = SpeechTtsService{};
    auto server = IpcSpeechTtsServer{options.service_name};
    auto worker = std::thread{[&] { signlang::speech_tts::playback_loop(options, service); }};

    while (!signlang::runtime::shutdown_requested()) {
      if (!server.wait_for_work(50)) {
        continue;
      }

      server.process_pending_requests([&](const signlang::speech_tts::SpeechTtsRequest& request) {
        const auto response = service.accept(request);
        if (response.status != signlang::speech_tts::SpeechTtsStatus::Ok) {
          spdlog::warn("Rejected speech TTS request {}: {}", request.request_id, response.message.data());
        }
        return response;
      });
    }

    service.stop();
    if (worker.joinable()) {
      worker.join();
    }
    return 0;
  });
}

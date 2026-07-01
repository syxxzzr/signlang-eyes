#ifndef SIGNLANG_EYES_SPEECH_TTS_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_SPEECH_TTS_ICEORYX_GATEWAY_HPP

#include "speech_tts_protocol.hpp"

#include "iox2/iceoryx2.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::speech_tts {

  class IpcSpeechTtsServer {
  public:
    explicit IpcSpeechTtsServer(const std::string& service_name);

    IpcSpeechTtsServer(const IpcSpeechTtsServer&) = delete;
    auto operator=(const IpcSpeechTtsServer&) -> IpcSpeechTtsServer& = delete;
    IpcSpeechTtsServer(IpcSpeechTtsServer&&) = delete;
    auto operator=(IpcSpeechTtsServer&&) -> IpcSpeechTtsServer& = delete;

    template <typename Handler>
    void process_pending_requests(Handler&& handler);

    [[nodiscard]] auto wait_for_work(std::uint64_t timeout_ms) -> bool;

  private:
    using TtsService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, SpeechTtsRequest, void, SpeechTtsResponse, void>;
    using TtsServer = iox2::Server<iox2::ServiceType::Ipc, SpeechTtsRequest, void, SpeechTtsResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> TtsService;
    static auto create_server(const TtsService& service) -> TtsServer;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    TtsService service_;
    TtsServer server_;
  };

  template <typename Handler>
  void IpcSpeechTtsServer::process_pending_requests(Handler&& handler) {
    auto receive_result = server_.receive();
    if (!receive_result.has_value()) {
      throw std::runtime_error("Failed to receive speech TTS request through iceoryx2");
    }

    auto active_request = std::move(receive_result.value());
    while (active_request.has_value()) {
      auto response = handler(active_request.value().payload());
      const auto send_result = active_request.value().send_copy(response);
      if (!send_result.has_value()) {
        throw std::runtime_error("Failed to send speech TTS response through iceoryx2");
      }

      receive_result = server_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive speech TTS request through iceoryx2");
      }
      active_request = std::move(receive_result.value());
    }
  }

} // namespace signlang::speech_tts

#endif // SIGNLANG_EYES_SPEECH_TTS_ICEORYX_GATEWAY_HPP

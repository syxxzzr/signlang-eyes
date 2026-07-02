#ifndef SIGNLANG_EYES_DATAFLOW_DISPATCHER_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_DATAFLOW_DISPATCHER_ICEORYX_GATEWAY_HPP

#include "common/ipc_utils.hpp"
#include "llm_client/llm_protocol.hpp"
#include "peripheral_service/peripheral_protocol.hpp"
#include "signlang_det/signlang_result.hpp"
#include "speech_asr/speech_asr_result.hpp"
#include "speech_tts/speech_tts_protocol.hpp"
#include "state_machine/app_state.hpp"

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::dataflow_dispatcher {

  class IpcStateSubscriber {
  public:
    IpcStateSubscriber(const std::string& event_service_name, const std::string& blackboard_service_name);

    IpcStateSubscriber(const IpcStateSubscriber&) = delete;
    auto operator=(const IpcStateSubscriber&) -> IpcStateSubscriber& = delete;
    IpcStateSubscriber(IpcStateSubscriber&&) = delete;
    auto operator=(IpcStateSubscriber&&) -> IpcStateSubscriber& = delete;

    [[nodiscard]] auto current_state() const -> signlang::state_machine::AppState;
    [[nodiscard]] auto poll_state_change() -> bool;
    [[nodiscard]] auto wait_for_state_change(std::uint64_t timeout_ms) -> bool;

  private:
    using BlackboardService =
        iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;
    using StateReader = iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;
    using StateEntry = iox2::EntryHandle<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey,
                                         signlang::state_machine::AppState>;
    using EventService = iox2::PortFactoryEvent<iox2::ServiceType::Ipc>;
    using EventListener = iox2::Listener<iox2::ServiceType::Ipc>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                        const std::string& service_name) -> BlackboardService;
    static auto create_reader(const BlackboardService& service) -> StateReader;
    static auto create_state_entry(StateReader& reader) -> StateEntry;
    static auto open_event_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> EventService;
    static auto create_listener(const EventService& service) -> EventListener;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    BlackboardService blackboard_service_;
    StateReader reader_;
    StateEntry state_entry_;
    EventService event_service_;
    EventListener listener_;
  };

  class IpcSignlangResultSubscriber {
  public:
    IpcSignlangResultSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size);

    IpcSignlangResultSubscriber(const IpcSignlangResultSubscriber&) = delete;
    auto operator=(const IpcSignlangResultSubscriber&) -> IpcSignlangResultSubscriber& = delete;
    IpcSignlangResultSubscriber(IpcSignlangResultSubscriber&&) = delete;
    auto operator=(IpcSignlangResultSubscriber&&) -> IpcSignlangResultSubscriber& = delete;

    [[nodiscard]] auto wait_for_work(std::uint64_t timeout_ms) -> bool;

    template <typename Handler>
    auto receive_latest(Handler&& handler) -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::signlang_det::SignlangResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, signlang::signlang_det::SignlangResult, void> subscriber_;
  };

  class IpcSpeechAsrResultSubscriber {
  public:
    IpcSpeechAsrResultSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size);

    IpcSpeechAsrResultSubscriber(const IpcSpeechAsrResultSubscriber&) = delete;
    auto operator=(const IpcSpeechAsrResultSubscriber&) -> IpcSpeechAsrResultSubscriber& = delete;
    IpcSpeechAsrResultSubscriber(IpcSpeechAsrResultSubscriber&&) = delete;
    auto operator=(IpcSpeechAsrResultSubscriber&&) -> IpcSpeechAsrResultSubscriber& = delete;

    [[nodiscard]] auto wait_for_work(std::uint64_t timeout_ms) -> bool;

    template <typename Handler>
    auto receive_latest(Handler&& handler) -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::speech_asr::SpeechAsrResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, signlang::speech_asr::SpeechAsrResult, void> subscriber_;
  };

  class IpcSpeechTtsClient {
  public:
    explicit IpcSpeechTtsClient(const std::string& service_name);

    IpcSpeechTtsClient(const IpcSpeechTtsClient&) = delete;
    auto operator=(const IpcSpeechTtsClient&) -> IpcSpeechTtsClient& = delete;
    IpcSpeechTtsClient(IpcSpeechTtsClient&&) = delete;
    auto operator=(IpcSpeechTtsClient&&) -> IpcSpeechTtsClient& = delete;

    [[nodiscard]] auto send_text(const std::string& text) -> signlang::speech_tts::SpeechTtsResponse;

  private:
    using TtsService = iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc,
                                                        signlang::speech_tts::SpeechTtsRequest, void,
                                                        signlang::speech_tts::SpeechTtsResponse, void>;
    using TtsClient = iox2::Client<iox2::ServiceType::Ipc, signlang::speech_tts::SpeechTtsRequest, void,
                                  signlang::speech_tts::SpeechTtsResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> TtsService;
    static auto create_client(const TtsService& service) -> TtsClient;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    TtsService service_;
    TtsClient client_;
    std::uint32_t next_request_id_{0};
  };

  class IpcLlmClient {
  public:
    explicit IpcLlmClient(const std::string& service_name);

    IpcLlmClient(const IpcLlmClient&) = delete;
    auto operator=(const IpcLlmClient&) -> IpcLlmClient& = delete;
    IpcLlmClient(IpcLlmClient&&) = delete;
    auto operator=(IpcLlmClient&&) -> IpcLlmClient& = delete;

    [[nodiscard]] auto send_prompt(const std::string& prompt) -> signlang::llm_client::LlmResponse;

  private:
    using LlmService = iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc,
                                                        signlang::llm_client::LlmRequest, void,
                                                        signlang::llm_client::LlmResponse, void>;
    using LlmClient = iox2::Client<iox2::ServiceType::Ipc, signlang::llm_client::LlmRequest, void,
                                  signlang::llm_client::LlmResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> LlmService;
    static auto create_client(const LlmService& service) -> LlmClient;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    LlmService service_;
    LlmClient client_;
    std::uint32_t next_request_id_{0};
  };

  class IpcDisplayClient {
  public:
    explicit IpcDisplayClient(const std::string& service_name);

    IpcDisplayClient(const IpcDisplayClient&) = delete;
    auto operator=(const IpcDisplayClient&) -> IpcDisplayClient& = delete;
    IpcDisplayClient(IpcDisplayClient&&) = delete;
    auto operator=(IpcDisplayClient&&) -> IpcDisplayClient& = delete;

    [[nodiscard]] auto set_title_line(const std::string& text) -> signlang::peripheral_service::DisplayResponse;
    [[nodiscard]] auto set_content_line(const std::string& text) -> signlang::peripheral_service::DisplayResponse;
    [[nodiscard]] auto clear_content_line() -> signlang::peripheral_service::DisplayResponse;

  private:
    using DisplayService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc,
                                         signlang::peripheral_service::DisplayRequest, void,
                                         signlang::peripheral_service::DisplayResponse, void>;
    using DisplayClient =
        iox2::Client<iox2::ServiceType::Ipc, signlang::peripheral_service::DisplayRequest, void,
                     signlang::peripheral_service::DisplayResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> DisplayService;
    static auto create_client(const DisplayService& service) -> DisplayClient;
    [[nodiscard]] auto send_display_request(signlang::peripheral_service::DisplayCommand command,
                                            const std::string& text)
        -> signlang::peripheral_service::DisplayResponse;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    DisplayService service_;
    DisplayClient client_;
    std::uint32_t next_request_id_{0};
  };

  template <typename Handler>
  auto IpcSignlangResultSubscriber::receive_latest(Handler&& handler) -> bool {
    return signlang::common::ipc::receive_latest_sample(
        subscriber_, "Failed to receive signlang result sample through iceoryx2 in dataflow dispatcher",
        std::forward<Handler>(handler));
  }

  template <typename Handler>
  auto IpcSpeechAsrResultSubscriber::receive_latest(Handler&& handler) -> bool {
    return signlang::common::ipc::receive_latest_sample(
        subscriber_, "Failed to receive ASR result sample through iceoryx2 in dataflow dispatcher",
        std::forward<Handler>(handler));
  }

} // namespace signlang::dataflow_dispatcher

#endif // SIGNLANG_EYES_DATAFLOW_DISPATCHER_ICEORYX_GATEWAY_HPP

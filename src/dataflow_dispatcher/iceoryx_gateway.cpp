#include "iceoryx_gateway.hpp"

#include "common/fixed_string.hpp"
#include "common/ipc_utils.hpp"

#include <stdexcept>
#include <utility>

namespace signlang::dataflow_dispatcher {
  namespace {
    constexpr std::uint64_t kMaxSignlangResultSubscribers = 4;
    constexpr std::uint64_t kMaxSpeechAsrResultSubscribers = 4;
    constexpr std::uint64_t kMaxStateEventListeners = 8;
    constexpr std::uint64_t kMaxStateEventNotifiers = 4;
    constexpr std::uint64_t kMaxStateEventNodes = 16;
    constexpr auto kMaxWaitAttempts = 100;
    constexpr auto kLlmMaxWaitAttempts = 7000;
    constexpr auto kResponseWaitMs = 10;
  }

  IpcStateSubscriber::IpcStateSubscriber(const std::string& event_service_name,
                                         const std::string& blackboard_service_name) :
      node_{create_node()},
      blackboard_service_{open_blackboard_service(node_, blackboard_service_name)},
      reader_{create_reader(blackboard_service_)}, state_entry_{create_state_entry(reader_)},
      event_service_{open_event_service(node_, event_service_name)}, listener_{create_listener(event_service_)} {}

  auto IpcStateSubscriber::current_state() const -> signlang::state_machine::AppState {
    auto value = state_entry_.get();
    return *value;
  }

  auto IpcStateSubscriber::poll_state_change() -> bool {
    auto changed = false;
    iox2::bb::StaticFunction<void(iox2::EventId)> callback{[&changed](iox2::EventId) { changed = true; }};
    const auto result = listener_.try_wait_all(callback);
    if (!result.has_value()) {
      throw std::runtime_error("Failed to poll app state event in dataflow dispatcher");
    }
    return changed;
  }

  auto IpcStateSubscriber::wait_for_state_change(std::uint64_t timeout_ms) -> bool {
    auto changed = false;
    iox2::bb::StaticFunction<void(iox2::EventId)> callback{[&changed](iox2::EventId) { changed = true; }};
    const auto result = listener_.timed_wait_all(callback, iox2::bb::Duration::from_millis(timeout_ms));
    if (!result.has_value()) {
      throw std::runtime_error("Failed to wait for app state event in dataflow dispatcher");
    }
    return changed;
  }

  auto IpcStateSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for dataflow dispatcher state subscriber");
  }

  auto IpcStateSubscriber::open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                   const std::string& service_name) -> BlackboardService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .blackboard_opener<signlang::state_machine::AppStateKey>()
                       .open();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open app state blackboard service in dataflow dispatcher: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcStateSubscriber::create_reader(const BlackboardService& service) -> StateReader {
    auto reader = service.reader_builder().create();
    if (!reader.has_value()) {
      throw std::runtime_error("Failed to create app state blackboard reader in dataflow dispatcher");
    }
    return std::move(reader.value());
  }

  auto IpcStateSubscriber::create_state_entry(StateReader& reader) -> StateEntry {
    auto entry = reader.entry<signlang::state_machine::AppState>(signlang::state_machine::default_app_state_key());
    if (!entry.has_value()) {
      throw std::runtime_error("Failed to create app state blackboard entry reader in dataflow dispatcher");
    }
    return std::move(entry.value());
  }

  auto IpcStateSubscriber::open_event_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                              const std::string& service_name) -> EventService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .event()
                       .max_notifiers(kMaxStateEventNotifiers)
                       .max_listeners(kMaxStateEventListeners)
                       .max_nodes(kMaxStateEventNodes)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open app state event service in dataflow dispatcher: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcStateSubscriber::create_listener(const EventService& service) -> EventListener {
    auto listener = service.listener_builder().create();
    if (!listener.has_value()) {
      throw std::runtime_error("Failed to create app state event listener in dataflow dispatcher");
    }
    return std::move(listener.value());
  }

  IpcSignlangResultSubscriber::IpcSignlangResultSubscriber(const std::string& service_name,
                                                           std::uint64_t subscriber_buffer_size) :
      node_{create_node()},
      subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

  auto IpcSignlangResultSubscriber::wait_for_work(std::uint64_t timeout_ms) -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(timeout_ms)).has_value();
  }

  auto IpcSignlangResultSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for dataflow dispatcher signlang subscriber");
  }

  auto IpcSignlangResultSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                      const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::signlang_det::SignlangResult, void> {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<signlang::signlang_det::SignlangResult>()
                       .max_subscribers(kMaxSignlangResultSubscribers)
                       .subscriber_max_buffer_size(buffer_size)
                       .subscriber_max_borrowed_samples(buffer_size)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open signlang result service in dataflow dispatcher: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create dataflow dispatcher signlang result subscriber");
    }
    return std::move(subscriber.value());
  }

  IpcSpeechAsrResultSubscriber::IpcSpeechAsrResultSubscriber(const std::string& service_name,
                                                             std::uint64_t subscriber_buffer_size) :
      node_{create_node()},
      subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

  auto IpcSpeechAsrResultSubscriber::wait_for_work(std::uint64_t timeout_ms) -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(timeout_ms)).has_value();
  }

  auto IpcSpeechAsrResultSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for dataflow dispatcher ASR subscriber");
  }

  auto IpcSpeechAsrResultSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                       const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::speech_asr::SpeechAsrResult, void> {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<signlang::speech_asr::SpeechAsrResult>()
                       .max_subscribers(kMaxSpeechAsrResultSubscribers)
                       .subscriber_max_buffer_size(buffer_size)
                       .subscriber_max_borrowed_samples(buffer_size)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open ASR result service in dataflow dispatcher: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create dataflow dispatcher ASR result subscriber");
    }
    return std::move(subscriber.value());
  }

  IpcSpeechTtsClient::IpcSpeechTtsClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  auto IpcSpeechTtsClient::send_text(const std::string& text) -> signlang::speech_tts::SpeechTtsResponse {
    auto request = signlang::speech_tts::SpeechTtsRequest{};
    request.request_id = next_request_id_++;
    signlang::common::copy_fixed_string(text, request.text);

    return signlang::common::ipc::send_request_and_wait_for_response(
        node_, client_, request, request.request_id, kMaxWaitAttempts, kResponseWaitMs,
        "Failed to send speech TTS request from dataflow dispatcher",
        "No speech TTS server received dataflow dispatcher request",
        "Failed to receive speech TTS response in dataflow dispatcher",
        "Received mismatched speech TTS response in dataflow dispatcher",
        "Timed out waiting for speech TTS response in dataflow dispatcher",
        [](const auto& response) { return response.request_id; });
  }

  auto IpcSpeechTtsClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for dataflow dispatcher speech TTS client");
  }

  auto IpcSpeechTtsClient::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                          const std::string& service_name) -> TtsService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<signlang::speech_tts::SpeechTtsRequest,
                                         signlang::speech_tts::SpeechTtsResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open speech TTS service in dataflow dispatcher: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcSpeechTtsClient::create_client(const TtsService& service) -> TtsClient {
    auto client = service.client_builder().create();
    if (!client.has_value()) {
      throw std::runtime_error("Failed to create speech TTS client in dataflow dispatcher");
    }
    return std::move(client.value());
  }

  IpcLlmClient::IpcLlmClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  auto IpcLlmClient::send_prompt(const std::string& prompt) -> signlang::llm_client::LlmResponse {
    auto request = signlang::llm_client::LlmRequest{};
    request.request_id = next_request_id_++;
    signlang::common::copy_fixed_string(prompt, request.prompt);

    return signlang::common::ipc::send_request_and_wait_for_response(
        node_, client_, request, request.request_id, kLlmMaxWaitAttempts, kResponseWaitMs,
        "Failed to send LLM request from dataflow dispatcher", "No LLM server received dataflow dispatcher request",
        "Failed to receive LLM response in dataflow dispatcher",
        "Received mismatched LLM response in dataflow dispatcher",
        "Timed out waiting for LLM response in dataflow dispatcher",
        [](const auto& response) { return response.request_id; });
  }

  auto IpcLlmClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for dataflow dispatcher LLM client");
  }

  auto IpcLlmClient::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                    const std::string& service_name) -> LlmService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<signlang::llm_client::LlmRequest, signlang::llm_client::LlmResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open LLM service in dataflow dispatcher: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcLlmClient::create_client(const LlmService& service) -> LlmClient {
    auto client = service.client_builder().create();
    if (!client.has_value()) {
      throw std::runtime_error("Failed to create LLM client in dataflow dispatcher");
    }
    return std::move(client.value());
  }

  IpcDisplayClient::IpcDisplayClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  auto IpcDisplayClient::set_title_line(const std::string& text) -> signlang::peripheral_service::DisplayResponse {
    return send_display_request(signlang::peripheral_service::DisplayCommand::SetTitleLine, text);
  }

  auto IpcDisplayClient::set_content_line(const std::string& text) -> signlang::peripheral_service::DisplayResponse {
    return send_display_request(signlang::peripheral_service::DisplayCommand::SetContentLine, text);
  }

  auto IpcDisplayClient::clear_content_line() -> signlang::peripheral_service::DisplayResponse {
    return send_display_request(signlang::peripheral_service::DisplayCommand::ClearContentLine, {});
  }

  auto IpcDisplayClient::send_display_request(signlang::peripheral_service::DisplayCommand command,
                                              const std::string& text)
      -> signlang::peripheral_service::DisplayResponse {
    if (!wait_for_server()) {
      throw std::runtime_error("No peripheral display server became available for dataflow dispatcher request");
    }

    auto request = signlang::peripheral_service::DisplayRequest{};
    request.request_id = next_request_id_++;
    request.command = command;
    signlang::common::copy_fixed_string(text, request.text);

    return signlang::common::ipc::send_request_and_wait_for_response(
        node_, client_, request, request.request_id, kMaxWaitAttempts, kResponseWaitMs,
        "Failed to send peripheral display request from dataflow dispatcher",
        "No peripheral display server received dataflow dispatcher request",
        "Failed to receive peripheral display response in dataflow dispatcher",
        "Received mismatched peripheral display response in dataflow dispatcher",
        "Timed out waiting for peripheral display response in dataflow dispatcher",
        [](const auto& response) { return response.request_id; });
  }

  auto IpcDisplayClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    return signlang::common::ipc::create_ipc_node(
        "Failed to create iceoryx2 node for dataflow dispatcher peripheral display client");
  }

  auto IpcDisplayClient::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                        const std::string& service_name) -> DisplayService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<signlang::peripheral_service::DisplayRequest,
                                         signlang::peripheral_service::DisplayResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open peripheral display service in dataflow dispatcher: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcDisplayClient::create_client(const DisplayService& service) -> DisplayClient {
    auto client = service.client_builder().create();
    if (!client.has_value()) {
      throw std::runtime_error("Failed to create peripheral display client in dataflow dispatcher");
    }
    return std::move(client.value());
  }

  auto IpcDisplayClient::wait_for_server() -> bool {
    for (auto attempt = 0; attempt < kMaxWaitAttempts; ++attempt) {
      if (signlang::common::ipc::has_servers(service_)) {
        return true;
      }
      (void)node_.wait(iox2::bb::Duration::from_millis(kResponseWaitMs));
    }
    return signlang::common::ipc::has_servers(service_);
  }

} // namespace signlang::dataflow_dispatcher

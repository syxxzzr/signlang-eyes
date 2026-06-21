#include "iceoryx_gateway.hpp"

#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::env_sound_det {
  namespace {

    auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
      const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
      if (!parsed_service_name.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
      }

      return parsed_service_name.value();
    }

  } // namespace

  IpcAudioSubscriber::IpcAudioSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size) :
      node_{create_node()}, subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

  auto IpcAudioSubscriber::receive_available(AudioRingBuffer& ring_buffer) -> AudioReceiveStats {
    AudioReceiveStats stats{};

    auto receive_result = subscriber_.receive();
    if (!receive_result.has_value()) {
      throw std::runtime_error("Failed to receive iceoryx2 audio frame sample");
    }

    auto sample = std::move(receive_result.value());
    while (sample.has_value()) {
      if (ring_buffer.push(sample->payload())) {
        ++stats.accepted_count;
      } else {
        ++stats.rejected_count;
      }

      receive_result = subscriber_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive iceoryx2 audio frame sample");
      }
      sample = std::move(receive_result.value());
    }

    return stats;
  }

  auto IpcAudioSubscriber::wait_for_work() -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(5)).has_value();
  }

  auto IpcAudioSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder()
                    .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                    .create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC audio subscriber node");
    }

    return std::move(node.value());
  }

  auto IpcAudioSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                             const std::string& service_name,
                                             std::uint64_t subscriber_buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::audio_frontend::AudioFrame, void> {
    auto service =
        node.service_builder(service_name_from_string(service_name)).publish_subscribe<signlang::audio_frontend::AudioFrame>().open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 audio service: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(subscriber_buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 audio subscriber for service: " + service_name);
    }

    return std::move(subscriber.value());
  }

  IpcStateControlClient::IpcStateControlClient(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, client_{create_client(service_)} {}

  void IpcStateControlClient::enter_dangerous_sound_state() const {
    const auto request = signlang::state_machine::StateControlRequest{
        .command = signlang::state_machine::StateControlCommand::EnterSpecial,
        .target_state = signlang::state_machine::AppState::DangerousSound,
        .timeout_ms = signlang::state_machine::kDefaultSpecialStateTimeoutMs,
    };

    (void)client_.send_copy(request);
  }

  auto IpcStateControlClient::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder()
                    .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                    .create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC state control client node");
    }

    return std::move(node.value());
  }

  auto IpcStateControlClient::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                             const std::string& service_name) -> StateControlService {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .request_response<signlang::state_machine::StateControlRequest,
                                         signlang::state_machine::StateControlResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 app state control request-response service: " +
                               service_name);
    }

    return std::move(service.value());
  }

  auto IpcStateControlClient::create_client(const StateControlService& service) -> StateControlClient {
    auto client = service.client_builder().create();
    if (!client.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state control client");
    }

    return std::move(client.value());
  }

} // namespace signlang::env_sound_det

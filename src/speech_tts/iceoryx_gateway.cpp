#include "iceoryx_gateway.hpp"

#include "common/ipc_utils.hpp"

#include <stdexcept>
#include <utility>

namespace signlang::speech_tts {

  IpcSpeechTtsServer::IpcSpeechTtsServer(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, server_{create_server(service_)} {}

  auto IpcSpeechTtsServer::wait_for_work(std::uint64_t timeout_ms) -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(timeout_ms)).has_value();
  }

  auto IpcSpeechTtsServer::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for speech TTS");
    }
    return std::move(node.value());
  }

  auto IpcSpeechTtsServer::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                          const std::string& service_name) -> TtsService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .request_response<SpeechTtsRequest, SpeechTtsResponse>()
                       .max_servers(1)
                       .max_clients(8)
                       .max_active_requests_per_client(1)
                       .max_response_buffer_size(1)
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create speech TTS request-response service: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcSpeechTtsServer::create_server(const TtsService& service) -> TtsServer {
    auto server = service.server_builder().create();
    if (!server.has_value()) {
      throw std::runtime_error("Failed to create speech TTS request-response server");
    }
    return std::move(server.value());
  }

} // namespace signlang::speech_tts

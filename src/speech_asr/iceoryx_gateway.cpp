#include "iceoryx_gateway.hpp"

#include "common/ipc_utils.hpp"

#include <chrono>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace signlang::speech_asr {
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

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC audio subscriber node");
    }

    return std::move(node.value());
  }

  auto IpcAudioSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                             const std::string& service_name, std::uint64_t subscriber_buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::audio_frontend::AudioFrame, void> {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .publish_subscribe<signlang::audio_frontend::AudioFrame>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 audio service: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(subscriber_buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 audio subscriber for service: " + service_name);
    }

    return std::move(subscriber.value());
  }

  IpcResultPublisher::IpcResultPublisher(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, publisher_{create_publisher(service_)} {}

  auto IpcResultPublisher::has_subscribers() const -> bool { return signlang::common::ipc::has_subscribers(service_); }

  void IpcResultPublisher::publish(const SpeechAsrResult& result) {
    auto loan_result = publisher_.loan_uninit();
    if (!loan_result.has_value()) {
      throw std::runtime_error("Failed to loan iceoryx2 speech ASR result sample");
    }

    auto loaned_sample = std::move(loan_result.value());
    auto initialized_sample = loaned_sample.write_payload(SpeechAsrResult{result});
    const auto send_result = iox2::send(std::move(initialized_sample));
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to publish speech ASR result through iceoryx2");
    }
  }

  auto IpcResultPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC ASR result publisher node");
    }

    return std::move(node.value());
  }

  auto IpcResultPublisher::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                          const std::string& service_name) -> ResultService {
    auto service = node.service_builder(signlang::common::ipc::service_name_from_string(service_name))
                       .publish_subscribe<SpeechAsrResult>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 speech ASR result service: " + service_name);
    }
    return std::move(service.value());
  }

  auto IpcResultPublisher::create_publisher(const ResultService& service)
      -> iox2::Publisher<iox2::ServiceType::Ipc, SpeechAsrResult, void> {
    auto publisher = service.publisher_builder().create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 speech ASR result publisher");
    }

    return std::move(publisher.value());
  }
} // namespace signlang::speech_asr

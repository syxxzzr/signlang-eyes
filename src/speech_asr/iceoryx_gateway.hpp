#ifndef SIGNLANG_EYES_EDGEAI_SPEECH_ASR_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_EDGEAI_SPEECH_ASR_ICEORYX_GATEWAY_HPP

#include "audio_ring_buffer.hpp"
#include "speech_asr_result.hpp"

#include "audio_frontend/audio_frame.hpp"
#include "iox2/iceoryx2.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace signlang::speech_asr {

  struct AudioReceiveStats {
    std::uint32_t accepted_count;
    std::uint32_t rejected_count;
  };

  class IpcAudioSubscriber {
  public:
    IpcAudioSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size);

    IpcAudioSubscriber(const IpcAudioSubscriber&) = delete;
    auto operator=(const IpcAudioSubscriber&) -> IpcAudioSubscriber& = delete;
    IpcAudioSubscriber(IpcAudioSubscriber&&) = delete;
    auto operator=(IpcAudioSubscriber&&) -> IpcAudioSubscriber& = delete;

    auto receive_available(AudioRingBuffer& ring_buffer) -> AudioReceiveStats;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t subscriber_buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang::audio_frontend::AudioFrame, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, signlang::audio_frontend::AudioFrame, void> subscriber_;
  };

  class IpcResultPublisher {
  public:
    explicit IpcResultPublisher(const std::string& service_name);

    IpcResultPublisher(const IpcResultPublisher&) = delete;
    auto operator=(const IpcResultPublisher&) -> IpcResultPublisher& = delete;
    IpcResultPublisher(IpcResultPublisher&&) = delete;
    auto operator=(IpcResultPublisher&&) -> IpcResultPublisher& = delete;

    void publish(const SpeechAsrResult& result);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Publisher<iox2::ServiceType::Ipc, SpeechAsrResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Publisher<iox2::ServiceType::Ipc, SpeechAsrResult, void> publisher_;
  };

  /// Event-driven ASR state monitor
  class IpcAsrStateMonitor {
  public:
    IpcAsrStateMonitor(const std::string& event_service_name, const std::string& blackboard_service_name);

    IpcAsrStateMonitor(const IpcAsrStateMonitor&) = delete;
    auto operator=(const IpcAsrStateMonitor&) -> IpcAsrStateMonitor& = delete;
    IpcAsrStateMonitor(IpcAsrStateMonitor&&) = delete;
    auto operator=(IpcAsrStateMonitor&&) -> IpcAsrStateMonitor& = delete;

    auto is_enabled() const -> bool;

    auto wait_for_state_change(std::chrono::milliseconds timeout) -> bool;

    auto try_wait_for_state_change() -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_listener(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Listener<iox2::ServiceType::Ipc>;
    static auto open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                       const std::string& service_name)
        -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AsrStateKey>;

    auto read_state_from_blackboard() -> AsrState;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Listener<iox2::ServiceType::Ipc> listener_;
    iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AsrStateKey> blackboard_service_;
    iox2::Reader<iox2::ServiceType::Ipc, AsrStateKey> reader_;
    AsrState cached_state_;
  };

} // namespace signlang::speech_asr

#endif // SIGNLANG_EYES_EDGEAI_SPEECH_ASR_ICEORYX_GATEWAY_HPP

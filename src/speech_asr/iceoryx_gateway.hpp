#ifndef SIGNLANG_EYES_SPEECH_ASR_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_SPEECH_ASR_ICEORYX_GATEWAY_HPP

#include "common/audio_ring_buffer.hpp"
#include "speech_asr_result.hpp"

#include "audio_frontend/audio_frame.hpp"
#include "iox2/iceoryx2.hpp"
#include "state_machine/app_state.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace signlang::speech_asr {

  using signlang::common::AudioRingBuffer;

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

    auto wait_for_work() -> bool;

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
    auto has_subscribers() const -> bool;

  private:
    using ResultService = iox2::PortFactoryPublishSubscribe<iox2::ServiceType::Ipc, SpeechAsrResult, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> ResultService;
    static auto create_publisher(const ResultService& service)
        -> iox2::Publisher<iox2::ServiceType::Ipc, SpeechAsrResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    ResultService service_;
    iox2::Publisher<iox2::ServiceType::Ipc, SpeechAsrResult, void> publisher_;
  };

  /// Event-driven global app state monitor for ASR activation.
  class IpcAsrStateMonitor {
  public:
    IpcAsrStateMonitor(const std::string& event_service_name, const std::string& blackboard_service_name);

    IpcAsrStateMonitor(const IpcAsrStateMonitor&) = delete;
    auto operator=(const IpcAsrStateMonitor&) -> IpcAsrStateMonitor& = delete;
    IpcAsrStateMonitor(IpcAsrStateMonitor&&) = delete;
    auto operator=(IpcAsrStateMonitor&&) -> IpcAsrStateMonitor& = delete;

    auto is_enabled() const -> bool;

    void wait_for_state_change_blocking();

    auto try_wait_for_state_change() -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_listener(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Listener<iox2::ServiceType::Ipc>;
    static auto open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;
    static auto create_reader(
        const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>& service)
        -> iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;

    auto read_state_from_blackboard() -> signlang::state_machine::AppState;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Listener<iox2::ServiceType::Ipc> listener_;
    iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> blackboard_service_;
    iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> reader_;
    signlang::state_machine::AppState cached_state_;
  };

} // namespace signlang::speech_asr

#endif // SIGNLANG_EYES_SPEECH_ASR_ICEORYX_GATEWAY_HPP

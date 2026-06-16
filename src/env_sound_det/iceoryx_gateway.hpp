#ifndef SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_ICEORYX_GATEWAY_HPP

#include "audio_ring_buffer.hpp"
#include "env_sound_result.hpp"

#include "audio_frontend/audio_frame.hpp"
#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <string>

namespace signlang::env_sound_det {

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

    void publish(const EnvSoundDetectionResult& result);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Publisher<iox2::ServiceType::Ipc, EnvSoundDetectionResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Publisher<iox2::ServiceType::Ipc, EnvSoundDetectionResult, void> publisher_;
  };

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_EDGEAI_ENV_SOUND_DET_ICEORYX_GATEWAY_HPP

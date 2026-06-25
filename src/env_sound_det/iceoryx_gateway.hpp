#ifndef SIGNLANG_EYES_ENV_SOUND_DET_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_ENV_SOUND_DET_ICEORYX_GATEWAY_HPP

#include "common/audio_ring_buffer.hpp"

#include "audio_frontend/audio_frame.hpp"
#include "iox2/iceoryx2.hpp"
#include "state_machine/state_control.hpp"

#include <cstdint>
#include <string>

namespace signlang::env_sound_det {

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

  class IpcStateControlClient {
  public:
    explicit IpcStateControlClient(const std::string& service_name);

    IpcStateControlClient(const IpcStateControlClient&) = delete;
    auto operator=(const IpcStateControlClient&) -> IpcStateControlClient& = delete;
    IpcStateControlClient(IpcStateControlClient&&) = delete;
    auto operator=(IpcStateControlClient&&) -> IpcStateControlClient& = delete;

    void enter_dangerous_sound_state() const;

  private:
    using StateControlService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, signlang::state_machine::StateControlRequest, void,
                                         signlang::state_machine::StateControlResponse, void>;
    using StateControlClient = iox2::Client<iox2::ServiceType::Ipc, signlang::state_machine::StateControlRequest, void,
                                            signlang::state_machine::StateControlResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> StateControlService;
    static auto create_client(const StateControlService& service) -> StateControlClient;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    StateControlService service_;
    StateControlClient client_;
  };

} // namespace signlang::env_sound_det

#endif // SIGNLANG_EYES_ENV_SOUND_DET_ICEORYX_GATEWAY_HPP

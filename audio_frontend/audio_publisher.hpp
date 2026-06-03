#ifndef SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_PUBLISHER_HPP
#define SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_PUBLISHER_HPP

#include "audio_frame.hpp"

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace signlang::audio_frontend {

  class AudioProcessor;

  class AudioPublisher {
  public:
    explicit AudioPublisher(const std::string& service_name);

    AudioPublisher(const AudioPublisher&) = delete;
    auto operator=(const AudioPublisher&) -> AudioPublisher& = delete;
    AudioPublisher(AudioPublisher&&) = delete;
    auto operator=(AudioPublisher&&) -> AudioPublisher& = delete;

    void publish(const std::vector<std::int16_t>& input_samples, AudioProcessor& audio_processor,
                 std::uint64_t sequence_number);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Publisher<iox2::ServiceType::Ipc, AudioFrame, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Publisher<iox2::ServiceType::Ipc, AudioFrame, void> publisher_;
  };

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_EDGEAI_AUDIO_FRONTEND_AUDIO_PUBLISHER_HPP

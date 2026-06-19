#ifndef SIGNLANG_EYES_AUDIO_FRONTEND_SOUND_SOURCE_BLACKBOARD_HPP
#define SIGNLANG_EYES_AUDIO_FRONTEND_SOUND_SOURCE_BLACKBOARD_HPP

#include "sound_source_localization.hpp"

#include "iox2/iceoryx2.hpp"

#include <string>

namespace signlang::audio_frontend {

  class SoundSourceBlackboardPublisher {
  public:
    explicit SoundSourceBlackboardPublisher(const std::string& service_name);

    SoundSourceBlackboardPublisher(const SoundSourceBlackboardPublisher&) = delete;
    auto operator=(const SoundSourceBlackboardPublisher&) -> SoundSourceBlackboardPublisher& = delete;
    SoundSourceBlackboardPublisher(SoundSourceBlackboardPublisher&&) = delete;
    auto operator=(SoundSourceBlackboardPublisher&&) -> SoundSourceBlackboardPublisher& = delete;

    void publish(const SoundSourceLocalizationResult& result);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, SoundSourceLocalizationKey>;
    static auto create_writer(
        const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, SoundSourceLocalizationKey>& service)
        -> iox2::Writer<iox2::ServiceType::Ipc, SoundSourceLocalizationKey>;
    static auto create_entry(iox2::Writer<iox2::ServiceType::Ipc, SoundSourceLocalizationKey>& writer)
        -> iox2::EntryHandleMut<iox2::ServiceType::Ipc, SoundSourceLocalizationKey, SoundSourceLocalizationResult>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, SoundSourceLocalizationKey> service_;
    iox2::Writer<iox2::ServiceType::Ipc, SoundSourceLocalizationKey> writer_;
    iox2::EntryHandleMut<iox2::ServiceType::Ipc, SoundSourceLocalizationKey, SoundSourceLocalizationResult> entry_;
  };

} // namespace signlang::audio_frontend

#endif // SIGNLANG_EYES_AUDIO_FRONTEND_SOUND_SOURCE_BLACKBOARD_HPP

#include "sound_source_blackboard.hpp"

#include "iox2/service_builder_blackboard_error.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::audio_frontend {
  namespace {

    auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
      const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
      if (!parsed_service_name.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
      }

      return parsed_service_name.value();
    }

    auto empty_localization_result() -> SoundSourceLocalizationResult {
      return SoundSourceLocalizationResult{
          .sequence_number = 0,
          .timestamp_ns = 0,
          .sample_rate_hz = 0,
          .frame_count = 0,
          .channel_count = 0,
          .strongest_channel = 0,
          .confidence = 0.0F,
          .valid = false,
          .proximity = {},
          .rms = {},
          .tdoa_score = {},
      };
    }

  } // namespace

  SoundSourceBlackboardPublisher::SoundSourceBlackboardPublisher(const std::string& service_name) :
      node_{create_node()}, service_{create_service(node_, service_name)}, writer_{create_writer(service_)},
      entry_{create_entry(writer_)} {
    publish(empty_localization_result());
  }

  void SoundSourceBlackboardPublisher::publish(const SoundSourceLocalizationResult& result) {
    entry_.update_with_copy(result);
  }

  auto SoundSourceBlackboardPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 audio source localization node");
    }

    return std::move(node.value());
  }

  auto SoundSourceBlackboardPublisher::create_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                      const std::string& service_name)
      -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, SoundSourceLocalizationKey> {
    const auto parsed_service_name = service_name_from_string(service_name);
    auto service = node.service_builder(parsed_service_name)
                       .blackboard_creator<SoundSourceLocalizationKey>()
                       .add<SoundSourceLocalizationResult>(default_sound_source_localization_key(),
                                                           empty_localization_result())
                       .create();
    if (service.has_value()) {
      return std::move(service.value());
    }

    const auto create_error = service.error();
    if (create_error != iox2::BlackboardCreateError::AlreadyExists) {
      throw std::runtime_error("Failed to create iceoryx2 sound source blackboard service: " + service_name);
    }

    auto opened_service = node.service_builder(parsed_service_name).blackboard_opener<SoundSourceLocalizationKey>().open();
    if (!opened_service.has_value()) {
      throw std::runtime_error("Failed to open existing iceoryx2 sound source blackboard service: " + service_name);
    }

    return std::move(opened_service.value());
  }

  auto SoundSourceBlackboardPublisher::create_writer(
      const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, SoundSourceLocalizationKey>& service)
      -> iox2::Writer<iox2::ServiceType::Ipc, SoundSourceLocalizationKey> {
    auto writer = service.writer_builder().create();
    if (!writer.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 sound source blackboard writer");
    }

    return std::move(writer.value());
  }

  auto SoundSourceBlackboardPublisher::create_entry(
      iox2::Writer<iox2::ServiceType::Ipc, SoundSourceLocalizationKey>& writer)
      -> iox2::EntryHandleMut<iox2::ServiceType::Ipc, SoundSourceLocalizationKey, SoundSourceLocalizationResult> {
    auto entry = writer.entry<SoundSourceLocalizationResult>(default_sound_source_localization_key());
    if (!entry.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 sound source blackboard entry writer");
    }

    return std::move(entry.value());
  }

} // namespace signlang::audio_frontend

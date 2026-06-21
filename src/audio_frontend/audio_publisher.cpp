#include "audio_publisher.hpp"

#include "audio_processor.hpp"

#include <chrono>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace signlang::audio_frontend {
  namespace {

    auto steady_timestamp_ns() -> std::uint64_t {
      const auto now = std::chrono::steady_clock::now().time_since_epoch();
      return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }

  } // namespace

  AudioPublisher::AudioPublisher(const std::string& service_name) :
      node_{create_node()}, publisher_{create_publisher(node_, service_name)} {}

  void AudioPublisher::publish(const std::vector<std::int16_t>& input_samples, AudioProcessor& audio_processor,
                               std::uint64_t sequence_number) {
    auto loan_result = publisher_.loan_uninit();
    if (!loan_result.has_value()) {
      throw std::runtime_error("Failed to loan iceoryx2 audio frame sample");
    }

    auto loaned_sample = std::move(loan_result.value());
    auto* frame = ::new (static_cast<void*>(&loaned_sample.payload_mut())) AudioFrame;
    const auto output_format = audio_processor.output_format();

    frame->sequence_number = sequence_number;
    frame->timestamp_ns = steady_timestamp_ns();
    frame->sample_rate_hz = output_format.sample_rate_hz;
    frame->publish_period_ms = audio_processor.publish_period_ms();
    frame->frame_count = audio_processor.output_frame_count();
    frame->channel_count = output_format.channel_count;
    frame->bits_per_sample = kBitsPerSample;

    audio_processor.process(input_samples, *frame);

    auto initialized_sample = iox2::assume_init(std::move(loaned_sample));
    const auto send_result = iox2::send(std::move(initialized_sample));
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to publish audio frame through iceoryx2");
    }
  }

  auto AudioPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder()
                    .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                    .create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC node");
    }

    return std::move(node.value());
  }

  auto AudioPublisher::create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
      -> iox2::Publisher<iox2::ServiceType::Ipc, AudioFrame, void> {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
    }

    auto service = node.service_builder(parsed_service_name.value()).publish_subscribe<AudioFrame>().open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 service: " + service_name);
    }

    auto publisher = service.value().publisher_builder().create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 publisher for service: " + service_name);
    }

    return std::move(publisher.value());
  }

} // namespace signlang::audio_frontend

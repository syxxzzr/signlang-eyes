#include "audio_frame.hpp"
#include "audio_format.hpp"

#include "cxxopts.hpp"
#include "iox2/iceoryx2.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

  struct ProgramOptions {
    std::string service_name;
    std::string output_path;
    std::uint32_t duration_sec;
    std::uint64_t subscriber_buffer_size;
  };

  auto parse_options(int argc, char** argv) -> ProgramOptions {
    cxxopts::Options options{
        "signlang_eyes_audio_frontend_record_test",
        "Subscribe to an audio_frontend iceoryx2 service and record PCM16 audio to a WAV file."};

    options.add_options()("s,service", "iceoryx2 audio service name", cxxopts::value<std::string>())(
        "o,output", "Output WAV file path",
        cxxopts::value<std::string>()->default_value("audio_frontend_record.wav"))(
        "duration-sec", "Recording duration in seconds", cxxopts::value<std::uint32_t>()->default_value("10"))(
        "buffer-size", "iceoryx2 subscriber buffer size", cxxopts::value<std::uint64_t>()->default_value("32"))(
        "h,help", "Print usage");

    const auto parsed_options = options.parse(argc, argv);
    if (parsed_options.count("help") != 0) {
      std::cout << options.help() << '\n';
      std::exit(0);
    }

    if (parsed_options.count("service") == 0) {
      throw std::runtime_error("--service is required.\n\n" + options.help());
    }

    const auto duration_sec = parsed_options["duration-sec"].as<std::uint32_t>();
    if (duration_sec == 0) {
      throw std::runtime_error("--duration-sec must be greater than zero");
    }

    const auto subscriber_buffer_size = parsed_options["buffer-size"].as<std::uint64_t>();
    if (subscriber_buffer_size == 0) {
      throw std::runtime_error("--buffer-size must be greater than zero");
    }

    return ProgramOptions{
        .service_name = parsed_options["service"].as<std::string>(),
        .output_path = parsed_options["output"].as<std::string>(),
        .duration_sec = duration_sec,
        .subscriber_buffer_size = subscriber_buffer_size,
    };
  }

  auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
    }

    return parsed_service_name.value();
  }

  void write_le16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
  }

  void write_le32(std::ofstream& output, std::uint32_t value) {
    write_le16(output, static_cast<std::uint16_t>(value & 0xFFFFU));
    write_le16(output, static_cast<std::uint16_t>((value >> 16U) & 0xFFFFU));
  }

  void write_wav_file(const std::string& output_path, const std::vector<std::int16_t>& samples,
                      std::uint32_t sample_rate_hz, std::uint16_t channel_count) {
    const auto data_size_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const auto byte_rate = sample_rate_hz * channel_count * sizeof(std::int16_t);
    const auto block_align = static_cast<std::uint16_t>(channel_count * sizeof(std::int16_t));

    std::ofstream output{output_path, std::ios::binary};
    if (!output.is_open()) {
      throw std::runtime_error("Failed to open WAV output file: " + output_path);
    }

    output.write("RIFF", 4);
    write_le32(output, 36U + data_size_bytes);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    write_le32(output, 16);
    write_le16(output, 1);
    write_le16(output, channel_count);
    write_le32(output, sample_rate_hz);
    write_le32(output, byte_rate);
    write_le16(output, block_align);
    write_le16(output, signlang::audio_frontend::kBitsPerSample);
    output.write("data", 4);
    write_le32(output, data_size_bytes);

    for (const auto sample : samples) {
      write_le16(output, static_cast<std::uint16_t>(sample));
    }

    if (!output.good()) {
      throw std::runtime_error("Failed while writing WAV output file: " + output_path);
    }
  }

  class AudioSubscriber {
  public:
    AudioSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size) :
        node_{create_node()}, subscriber_{create_subscriber(node_, service_name, subscriber_buffer_size)} {}

    template <typename Handler>
    auto receive_available(Handler&& handler) -> std::uint32_t {
      std::uint32_t received_count = 0;

      auto receive_result = subscriber_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive iceoryx2 audio frame sample");
      }

      auto sample = std::move(receive_result.value());
      while (sample.has_value()) {
        handler(sample->payload());
        ++received_count;

        receive_result = subscriber_.receive();
        if (!receive_result.has_value()) {
          throw std::runtime_error("Failed to receive iceoryx2 audio frame sample");
        }
        sample = std::move(receive_result.value());
      }

      return received_count;
    }

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
      iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

      auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
      if (!node.has_value()) {
        throw std::runtime_error("Failed to create iceoryx2 IPC node");
      }

      return std::move(node.value());
    }

    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t subscriber_buffer_size)
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

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, signlang::audio_frontend::AudioFrame, void> subscriber_;
  };

} // namespace

auto main(int argc, char** argv) -> int {
  try {
    const auto options = parse_options(argc, argv);
    AudioSubscriber subscriber{options.service_name, options.subscriber_buffer_size};

    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate_hz = 0;
    std::uint16_t channel_count = 0;
    std::uint32_t received_frames = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{options.duration_sec};

    while (std::chrono::steady_clock::now() < deadline) {
      const auto received_count = subscriber.receive_available([&](const signlang::audio_frontend::AudioFrame& frame) {
        if (frame.bits_per_sample != signlang::audio_frontend::kBitsPerSample) {
          throw std::runtime_error("Only PCM16 audio frames are supported");
        }

        if (sample_rate_hz == 0) {
          sample_rate_hz = frame.sample_rate_hz;
          channel_count = frame.channel_count;
        } else if (frame.sample_rate_hz != sample_rate_hz || frame.channel_count != channel_count) {
          throw std::runtime_error("Audio format changed while recording");
        }

        const auto sample_count = static_cast<std::uint64_t>(frame.frame_count) * frame.channel_count;
        if (sample_count > frame.samples.size()) {
          throw std::runtime_error("Audio frame sample count exceeds payload capacity");
        }

        samples.insert(samples.end(), frame.samples.begin(), frame.samples.begin() + sample_count);
        ++received_frames;
      });

      if (received_count == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }

    if (samples.empty() || sample_rate_hz == 0 || channel_count == 0) {
      throw std::runtime_error("No audio frames were received from service: " + options.service_name);
    }

    write_wav_file(options.output_path, samples, sample_rate_hz, channel_count);
    std::cout << "Recorded " << received_frames << " audio frames to " << options.output_path << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#ifndef SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_ICEORYX_GATEWAY_HPP

#include "signlang_result.hpp"
#include "handpose_det/handpose_frame.hpp"

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <string>

namespace signlang::signlang_det {

class IpcHandposeSubscriber {
public:
  IpcHandposeSubscriber(const std::string& service_name,
                        std::uint64_t subscriber_buffer_size);

  IpcHandposeSubscriber(const IpcHandposeSubscriber&) = delete;
  auto operator=(const IpcHandposeSubscriber&) -> IpcHandposeSubscriber& = delete;
  IpcHandposeSubscriber(IpcHandposeSubscriber&&) = delete;
  auto operator=(IpcHandposeSubscriber&&) -> IpcHandposeSubscriber& = delete;

  template <typename Handler>
  auto receive_latest(Handler&& handler) -> bool;

private:
  static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
  static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                const std::string& service_name,
                                std::uint64_t buffer_size)
    -> iox2::Subscriber<iox2::ServiceType::Ipc,
                        iox2::bb::Slice<handpose_det::HandPoseDetection>,
                        handpose_det::HandPoseFrameMetadata>;

  iox2::Node<iox2::ServiceType::Ipc> node_;
  iox2::Subscriber<iox2::ServiceType::Ipc,
                   iox2::bb::Slice<handpose_det::HandPoseDetection>,
                   handpose_det::HandPoseFrameMetadata> subscriber_;
};

class IpcSignlangPublisher {
public:
  explicit IpcSignlangPublisher(const std::string& service_name);

  IpcSignlangPublisher(const IpcSignlangPublisher&) = delete;
  auto operator=(const IpcSignlangPublisher&) -> IpcSignlangPublisher& = delete;
  IpcSignlangPublisher(IpcSignlangPublisher&&) = delete;
  auto operator=(IpcSignlangPublisher&&) -> IpcSignlangPublisher& = delete;

  void publish(const SignlangResult& result);

private:
  static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
  static auto create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node,
                               const std::string& service_name)
    -> iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void>;

  iox2::Node<iox2::ServiceType::Ipc> node_;
  iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void> publisher_;
  std::uint64_t sequence_number_{0};
};

template <typename Handler>
auto IpcHandposeSubscriber::receive_latest(Handler&& handler) -> bool {
  auto latest_sample = subscriber_.receive();
  if (!latest_sample.has_value()) {
    throw std::runtime_error("Failed to receive handpose sample through iceoryx2");
  }

  if (!latest_sample.value().has_value()) {
    return false;
  }

  while (true) {
    auto next_sample = subscriber_.receive();
    if (!next_sample.has_value()) {
      throw std::runtime_error("Failed to receive handpose sample through iceoryx2");
    }
    if (!next_sample.value().has_value()) {
      break;
    }
    latest_sample = std::move(next_sample);
  }

  const auto& current_sample = latest_sample.value().value();
  const auto payload = current_sample.payload();
  const auto& metadata = current_sample.user_header();

  handler(metadata, payload.data(), payload.number_of_elements());
  return true;
}

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_EDGEAI_SIGNLANG_DET_ICEORYX_GATEWAY_HPP

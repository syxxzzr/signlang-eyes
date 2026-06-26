#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_ICEORYX_GATEWAY_HPP

#include "handpose_det/handpose_frame.hpp"
#include "signlang_det/prototype_control.hpp"

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::signlang_manager {

  class IpcHandposeSubscriber {
  public:
    IpcHandposeSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size);

    IpcHandposeSubscriber(const IpcHandposeSubscriber&) = delete;
    auto operator=(const IpcHandposeSubscriber&) -> IpcHandposeSubscriber& = delete;
    IpcHandposeSubscriber(IpcHandposeSubscriber&&) = delete;
    auto operator=(IpcHandposeSubscriber&&) -> IpcHandposeSubscriber& = delete;

    template <typename Handler>
    auto receive_latest(Handler&& handler) -> bool;

    auto wait_for_work() -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<handpose_det::HandPoseDetection>,
                            handpose_det::HandPoseFrameMetadata>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<handpose_det::HandPoseDetection>,
                     handpose_det::HandPoseFrameMetadata>
        subscriber_;
  };

  class IpcPrototypeControlClient {
  public:
    explicit IpcPrototypeControlClient(const std::string& service_name);

    IpcPrototypeControlClient(const IpcPrototypeControlClient&) = delete;
    auto operator=(const IpcPrototypeControlClient&) -> IpcPrototypeControlClient& = delete;
    IpcPrototypeControlClient(IpcPrototypeControlClient&&) = delete;
    auto operator=(IpcPrototypeControlClient&&) -> IpcPrototypeControlClient& = delete;

    void request_reload() const;

  private:
    using PrototypeControlService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, signlang_det::PrototypeControlRequest, void,
                                         signlang_det::PrototypeControlResponse, void>;
    using PrototypeControlClient = iox2::Client<iox2::ServiceType::Ipc, signlang_det::PrototypeControlRequest, void,
                                                signlang_det::PrototypeControlResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> PrototypeControlService;
    static auto create_client(const PrototypeControlService& service) -> PrototypeControlClient;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    PrototypeControlService service_;
    PrototypeControlClient client_;
  };

  template <typename Handler>
  auto IpcHandposeSubscriber::receive_latest(Handler&& handler) -> bool {
    auto latest_sample = subscriber_.receive();
    if (!latest_sample.has_value()) {
      throw std::runtime_error("Failed to receive handpose sample through iceoryx2 in signlang manager");
    }

    if (!latest_sample.value().has_value()) {
      return false;
    }

    while (true) {
      auto next_sample = subscriber_.receive();
      if (!next_sample.has_value()) {
        throw std::runtime_error("Failed to receive handpose sample through iceoryx2 in signlang manager");
      }
      if (!next_sample.value().has_value()) {
        break;
      }
      latest_sample = std::move(next_sample);
    }

    const auto& current_sample = latest_sample.value().value();
    const auto payload = current_sample.payload();
    const auto& metadata = current_sample.user_header();
    handler(metadata, payload.data(), metadata.detection_count);
    return true;
  }

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_ICEORYX_GATEWAY_HPP

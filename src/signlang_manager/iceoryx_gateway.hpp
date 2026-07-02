#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_ICEORYX_GATEWAY_HPP

#include "handpose_det/handpose_frame.hpp"
#include "signlang_det/gesture_management.hpp"
#include "signlang_det/prototype_control.hpp"
#include "signlang_det/signlang_result.hpp"

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

    [[nodiscard]] auto wait_for_work() -> bool;

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

  class IpcGestureManagementClient {
  public:
    explicit IpcGestureManagementClient(const std::string& service_name);

    IpcGestureManagementClient(const IpcGestureManagementClient&) = delete;
    auto operator=(const IpcGestureManagementClient&) -> IpcGestureManagementClient& = delete;
    IpcGestureManagementClient(IpcGestureManagementClient&&) = delete;
    auto operator=(IpcGestureManagementClient&&) -> IpcGestureManagementClient& = delete;

    [[nodiscard]] auto request(const signlang_det::GestureManagementRequest& request) const
        -> signlang_det::GestureManagementResponse;

  private:
    using GestureManagementService =
        iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, signlang_det::GestureManagementRequest, void,
                                         signlang_det::GestureManagementResponse, void>;
    using GestureManagementClient = iox2::Client<iox2::ServiceType::Ipc, signlang_det::GestureManagementRequest, void,
                                                signlang_det::GestureManagementResponse, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> GestureManagementService;
    static auto create_client(const GestureManagementService& service) -> GestureManagementClient;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    GestureManagementService service_;
    GestureManagementClient client_;
  };

  class IpcSignlangResultSubscriber {
  public:
    IpcSignlangResultSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size);

    IpcSignlangResultSubscriber(const IpcSignlangResultSubscriber&) = delete;
    auto operator=(const IpcSignlangResultSubscriber&) -> IpcSignlangResultSubscriber& = delete;
    IpcSignlangResultSubscriber(IpcSignlangResultSubscriber&&) = delete;
    auto operator=(IpcSignlangResultSubscriber&&) -> IpcSignlangResultSubscriber& = delete;

    template <typename Handler>
    auto receive_latest(Handler&& handler) -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name,
                                  std::uint64_t buffer_size)
        -> iox2::Subscriber<iox2::ServiceType::Ipc, signlang_det::SignlangResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Subscriber<iox2::ServiceType::Ipc, signlang_det::SignlangResult, void> subscriber_;
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

  template <typename Handler>
  auto IpcSignlangResultSubscriber::receive_latest(Handler&& handler) -> bool {
    auto latest_sample = subscriber_.receive();
    if (!latest_sample.has_value()) {
      throw std::runtime_error("Failed to receive signlang result sample through iceoryx2 in signlang manager");
    }

    if (!latest_sample.value().has_value()) {
      return false;
    }

    while (true) {
      auto next_sample = subscriber_.receive();
      if (!next_sample.has_value()) {
        throw std::runtime_error("Failed to receive signlang result sample through iceoryx2 in signlang manager");
      }
      if (!next_sample.value().has_value()) {
        break;
      }
      latest_sample = std::move(next_sample);
    }

    handler(latest_sample.value().value().payload());
    return true;
  }

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_ICEORYX_GATEWAY_HPP

#ifndef SIGNLANG_EYES_SIGNLANG_DET_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_ICEORYX_GATEWAY_HPP

#include "handpose_det/handpose_frame.hpp"
#include "prototype_control.hpp"
#include "signlang_result.hpp"

#include "iox2/iceoryx2.hpp"
#include "state_machine/app_state.hpp"

#include <cstdint>
#include <string>

namespace signlang::signlang_det {

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

  class IpcSignlangPublisher {
  public:
    explicit IpcSignlangPublisher(const std::string& service_name);

    IpcSignlangPublisher(const IpcSignlangPublisher&) = delete;
    auto operator=(const IpcSignlangPublisher&) -> IpcSignlangPublisher& = delete;
    IpcSignlangPublisher(IpcSignlangPublisher&&) = delete;
    auto operator=(IpcSignlangPublisher&&) -> IpcSignlangPublisher& = delete;

    void publish(const SignlangResult& result);
    auto has_subscribers() const -> bool;

  private:
    using ResultService = iox2::PortFactoryPublishSubscribe<iox2::ServiceType::Ipc, SignlangResult, void>;

    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> ResultService;
    static auto create_publisher(const ResultService& service)
        -> iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    ResultService service_;
    iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void> publisher_;
    std::uint64_t sequence_number_{0};
  };

  class IpcPrototypeControlServer {
  public:
    explicit IpcPrototypeControlServer(const std::string& service_name);

    IpcPrototypeControlServer(const IpcPrototypeControlServer&) = delete;
    auto operator=(const IpcPrototypeControlServer&) -> IpcPrototypeControlServer& = delete;
    IpcPrototypeControlServer(IpcPrototypeControlServer&&) = delete;
    auto operator=(IpcPrototypeControlServer&&) -> IpcPrototypeControlServer& = delete;

    template <typename Handler>
    void process_pending_requests(Handler&& handler);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, PrototypeControlRequest, void,
                                            PrototypeControlResponse, void>;
    static auto create_server(const iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, PrototypeControlRequest,
                                                                     void, PrototypeControlResponse, void>& service)
        -> iox2::Server<iox2::ServiceType::Ipc, PrototypeControlRequest, void, PrototypeControlResponse, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, PrototypeControlRequest, void, PrototypeControlResponse,
                                     void>
        service_;
    iox2::Server<iox2::ServiceType::Ipc, PrototypeControlRequest, void, PrototypeControlResponse, void> server_;
  };

  /// Event-driven sign language detection state monitor
  class IpcSignlangDetStateMonitor {
  public:
    IpcSignlangDetStateMonitor(const std::string& event_service_name, const std::string& blackboard_service_name);

    IpcSignlangDetStateMonitor(const IpcSignlangDetStateMonitor&) = delete;
    auto operator=(const IpcSignlangDetStateMonitor&) -> IpcSignlangDetStateMonitor& = delete;
    IpcSignlangDetStateMonitor(IpcSignlangDetStateMonitor&&) = delete;
    auto operator=(IpcSignlangDetStateMonitor&&) -> IpcSignlangDetStateMonitor& = delete;

    auto is_enabled() const -> bool;

    void wait_for_state_change_blocking();

    auto try_wait_for_state_change() -> bool;

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_listener(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::Listener<iox2::ServiceType::Ipc>;
    static auto open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;
    static auto create_reader(
        const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>& service)
        -> iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>;

    auto read_state_from_blackboard() -> signlang::state_machine::AppState;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::Listener<iox2::ServiceType::Ipc> listener_;
    iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> blackboard_service_;
    iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> reader_;
    signlang::state_machine::AppState cached_state_;
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

    handler(metadata, payload.data(), metadata.detection_count);
    return true;
  }

  template <typename Handler>
  void IpcPrototypeControlServer::process_pending_requests(Handler&& handler) {
    auto receive_result = server_.receive();
    if (!receive_result.has_value()) {
      throw std::runtime_error("Failed to receive signlang prototype control request");
    }

    auto active_request = std::move(receive_result.value());
    while (active_request.has_value()) {
      auto response = handler(active_request.value().payload());
      const auto send_result = active_request.value().send_copy(response);
      if (!send_result.has_value()) {
        throw std::runtime_error("Failed to send signlang prototype control response");
      }

      receive_result = server_.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error("Failed to receive signlang prototype control request");
      }
      active_request = std::move(receive_result.value());
    }
  }

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_ICEORYX_GATEWAY_HPP

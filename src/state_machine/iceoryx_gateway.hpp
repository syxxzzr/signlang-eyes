#ifndef SIGNLANG_EYES_STATE_MACHINE_ICEORYX_GATEWAY_HPP
#define SIGNLANG_EYES_STATE_MACHINE_ICEORYX_GATEWAY_HPP

#include "app_state.hpp"
#include "state_control.hpp"

#include "iox2/iceoryx2.hpp"

#include <string>

namespace signlang::state_machine {

  class IpcStatePublisher {
  public:
    IpcStatePublisher(const std::string& event_service_name, const std::string& blackboard_service_name,
                      AppState initial_state);

    IpcStatePublisher(const IpcStatePublisher&) = delete;
    auto operator=(const IpcStatePublisher&) -> IpcStatePublisher& = delete;
    IpcStatePublisher(IpcStatePublisher&&) = delete;
    auto operator=(IpcStatePublisher&&) -> IpcStatePublisher& = delete;

    auto current_state() const -> AppState;
    void set_state(AppState state);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                          const std::string& service_name, AppState initial_state)
        -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AppStateKey>;
    static auto create_writer(const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AppStateKey>& service)
        -> iox2::Writer<iox2::ServiceType::Ipc, AppStateKey>;
    static auto create_state_entry(iox2::Writer<iox2::ServiceType::Ipc, AppStateKey>& writer)
        -> iox2::EntryHandleMut<iox2::ServiceType::Ipc, AppStateKey, AppState>;
    static auto create_event_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryEvent<iox2::ServiceType::Ipc>;
    static auto create_notifier(const iox2::PortFactoryEvent<iox2::ServiceType::Ipc>& service)
        -> iox2::Notifier<iox2::ServiceType::Ipc>;

    void publish_current_state();

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, AppStateKey> blackboard_service_;
    iox2::Writer<iox2::ServiceType::Ipc, AppStateKey> writer_;
    iox2::EntryHandleMut<iox2::ServiceType::Ipc, AppStateKey, AppState> state_entry_;
    iox2::PortFactoryEvent<iox2::ServiceType::Ipc> event_service_;
    iox2::Notifier<iox2::ServiceType::Ipc> notifier_;
    AppState current_state_;
  };

  class IpcStateControlServer {
  public:
    explicit IpcStateControlServer(const std::string& service_name);

    IpcStateControlServer(const IpcStateControlServer&) = delete;
    auto operator=(const IpcStateControlServer&) -> IpcStateControlServer& = delete;
    IpcStateControlServer(IpcStateControlServer&&) = delete;
    auto operator=(IpcStateControlServer&&) -> IpcStateControlServer& = delete;

    void process_pending_requests(StateController& controller, IpcStatePublisher& publisher,
                                  StateController::Clock::time_point now);

  private:
    static auto create_node() -> iox2::Node<iox2::ServiceType::Ipc>;
    static auto create_service(const iox2::Node<iox2::ServiceType::Ipc>& node, const std::string& service_name)
        -> iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse,
                                            void>;
    static auto create_server(const iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, StateControlRequest, void,
                                                                     StateControlResponse, void>& service)
        -> iox2::Server<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse, void>;

    iox2::Node<iox2::ServiceType::Ipc> node_;
    iox2::PortFactoryRequestResponse<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse, void>
        service_;
    iox2::Server<iox2::ServiceType::Ipc, StateControlRequest, void, StateControlResponse, void> server_;
  };

} // namespace signlang::state_machine

#endif // SIGNLANG_EYES_STATE_MACHINE_ICEORYX_GATEWAY_HPP

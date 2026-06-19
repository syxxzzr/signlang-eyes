#include "iceoryx_gateway.hpp"

#include <stdexcept>
#include <utility>

namespace signlang::signlang_det {
  namespace {

    auto service_name_from_string(const std::string& name) -> iox2::ServiceName {
      auto result = iox2::ServiceName::create(name.c_str());
      if (!result.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + name);
      }
      return std::move(result.value());
    }

  } // namespace

  auto IpcHandposeSubscriber::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    auto node_result = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node_result.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for handpose subscriber");
    }
    return std::move(node_result.value());
  }

  auto IpcHandposeSubscriber::create_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<handpose_det::HandPoseDetection>,
                          handpose_det::HandPoseFrameMetadata> {
    auto service_result = node.service_builder(service_name_from_string(service_name))
                              .publish_subscribe<iox2::bb::Slice<handpose_det::HandPoseDetection>>()
                              .user_header<handpose_det::HandPoseFrameMetadata>()
                              .open_or_create();

    if (!service_result.has_value()) {
      throw std::runtime_error("Failed to open handpose service: " + service_name);
    }

    auto subscriber_result = service_result.value().subscriber_builder().buffer_size(buffer_size).create();

    if (!subscriber_result.has_value()) {
      throw std::runtime_error("Failed to create handpose subscriber");
    }

    return std::move(subscriber_result.value());
  }

  IpcHandposeSubscriber::IpcHandposeSubscriber(const std::string& service_name, std::uint64_t subscriber_buffer_size) :
      node_(create_node()), subscriber_(create_subscriber(node_, service_name, subscriber_buffer_size)) {}

  auto IpcSignlangPublisher::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    auto node_result = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node_result.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 node for signlang publisher");
    }
    return std::move(node_result.value());
  }

  auto IpcSignlangPublisher::create_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                              const std::string& service_name)
      -> iox2::Publisher<iox2::ServiceType::Ipc, SignlangResult, void> {
    auto service_result = node.service_builder(service_name_from_string(SignlangResult::IOX2_TYPE_NAME))
                              .publish_subscribe<SignlangResult>()
                              .open_or_create();

    if (!service_result.has_value()) {
      throw std::runtime_error("Failed to open signlang result service: " + service_name);
    }

    auto publisher_result = service_result.value().publisher_builder().create();

    if (!publisher_result.has_value()) {
      throw std::runtime_error("Failed to create signlang result publisher");
    }

    return std::move(publisher_result.value());
  }

  IpcSignlangPublisher::IpcSignlangPublisher(const std::string& service_name) :
      node_(create_node()), publisher_(create_publisher(node_, service_name)) {
    static_cast<void>(service_name);
  }

  void IpcSignlangPublisher::publish(const SignlangResult& result) {
    auto loan_result = publisher_.loan_uninit();
    if (!loan_result.has_value()) {
      throw std::runtime_error("Failed to loan iceoryx2 signlang result sample");
    }

    auto loaned_sample = std::move(loan_result.value());
    auto result_copy = result;
    result_copy.sequence_number = sequence_number_++;

    auto initialized_sample = loaned_sample.write_payload(result_copy);
    const auto send_result = iox2::send(std::move(initialized_sample));
    if (!send_result.has_value()) {
      throw std::runtime_error("Failed to publish signlang result through iceoryx2");
    }
  }

  IpcSignlangDetStateMonitor::IpcSignlangDetStateMonitor(const std::string& event_service_name,
                                                         const std::string& blackboard_service_name) :
      node_{create_node()},
      listener_{create_listener(node_, event_service_name)}, blackboard_service_{open_blackboard_service(
                                                                 node_, blackboard_service_name)},
      reader_{create_reader(blackboard_service_)}, cached_state_{signlang::state_machine::AppState::Normal} {
    cached_state_ = read_state_from_blackboard();
  }

  auto IpcSignlangDetStateMonitor::is_enabled() const -> bool {
    return cached_state_ == signlang::state_machine::AppState::SignLanguageChat ||
        cached_state_ == signlang::state_machine::AppState::SignLanguageAi;
  }

  void IpcSignlangDetStateMonitor::wait_for_state_change_blocking() {
    auto result = listener_.blocking_wait([](iox2::EventActivation /* event */) {});

    if (!result.has_value()) {
      throw std::runtime_error("Failed to wait for global app state change event in sign language detection");
    }

    cached_state_ = read_state_from_blackboard();
  }

  auto IpcSignlangDetStateMonitor::try_wait_for_state_change() -> bool {
    bool event_received = false;

    auto result = listener_.try_wait([&event_received](iox2::EventActivation /* event */) { event_received = true; });

    if (!result.has_value()) {
      throw std::runtime_error("Failed to check for global app state change event in sign language detection");
    }

    if (event_received) {
      cached_state_ = read_state_from_blackboard();
    }

    return event_received;
  }

  auto IpcSignlangDetStateMonitor::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC sign language detection state monitor node");
    }

    return std::move(node.value());
  }

  auto IpcSignlangDetStateMonitor::create_listener(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                   const std::string& service_name)
      -> iox2::Listener<iox2::ServiceType::Ipc> {
    auto service = node.service_builder(service_name_from_string(service_name)).event().open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error(
          "Failed to open or create iceoryx2 app state event service for sign language detection: " + service_name);
    }

    auto listener = service.value().listener_builder().create();
    if (!listener.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state listener for sign language detection service: " +
                               service_name);
    }

    return std::move(listener.value());
  }

  auto IpcSignlangDetStateMonitor::open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                           const std::string& service_name)
      -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .blackboard_opener<signlang::state_machine::AppStateKey>()
                       .open();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open iceoryx2 app state blackboard service for sign language detection: " +
                               service_name);
    }

    return std::move(service.value());
  }

  auto IpcSignlangDetStateMonitor::create_reader(
      const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>& service)
      -> iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> {
    auto reader = service.reader_builder().create();
    if (!reader.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state blackboard reader for sign language detection");
    }

    return std::move(reader.value());
  }

  auto IpcSignlangDetStateMonitor::read_state_from_blackboard() -> signlang::state_machine::AppState {
    auto entry_result =
        reader_.entry<signlang::state_machine::AppState>(signlang::state_machine::default_app_state_key());
    if (!entry_result.has_value()) {
      return signlang::state_machine::AppState::Normal;
    }

    auto entry = std::move(entry_result.value());
    auto blackboard_value = entry.get();
    return *blackboard_value;
  }

} // namespace signlang::signlang_det

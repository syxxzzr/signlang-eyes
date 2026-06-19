#include "handpose_transport.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace signlang::handpose_det {
  namespace {

    constexpr auto kWaitPeriodMs = std::uint64_t{5};

    auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
      const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
      if (!parsed_service_name.has_value()) {
        throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
      }

      return parsed_service_name.value();
    }

  } // namespace

  HandPoseTransport::HandPoseTransport(const std::string& input_service_name, const std::string& output_service_name,
                                       std::uint64_t subscriber_buffer_size, std::uint32_t max_detections) :
      node_{create_node()},
      subscriber_{create_video_subscriber(node_, input_service_name, subscriber_buffer_size)},
      publisher_{create_handpose_publisher(node_, output_service_name, max_detections)}, max_detections_{
                                                                                             max_detections} {}

  auto HandPoseTransport::wait_for_work() -> bool {
    return node_.wait(iox2::bb::Duration::from_millis(kWaitPeriodMs)).has_value();
  }

  auto HandPoseTransport::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);
    auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC node");
    }

    return std::move(node.value());
  }

  auto HandPoseTransport::create_video_subscriber(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                  const std::string& service_name, std::uint64_t buffer_size)
      -> iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<std::uint8_t>,
                          signlang::video_frontend::VideoFrameMetadata> {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .publish_subscribe<iox2::bb::Slice<std::uint8_t>>()
                       .user_header<signlang::video_frontend::VideoFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open upstream iceoryx2 service: " + service_name);
    }

    auto subscriber = service.value().subscriber_builder().buffer_size(buffer_size).create();
    if (!subscriber.has_value()) {
      throw std::runtime_error("Failed to create upstream video subscriber: " + service_name);
    }

    return std::move(subscriber.value());
  }

  auto HandPoseTransport::create_handpose_publisher(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                    const std::string& service_name, std::uint32_t max_detections)
      -> iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<HandPoseDetection>, HandPoseFrameMetadata> {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .publish_subscribe<iox2::bb::Slice<HandPoseDetection>>()
                       .user_header<HandPoseFrameMetadata>()
                       .open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create handpose iceoryx2 service: " + service_name);
    }

    auto publisher = service.value()
                         .publisher_builder()
                         .initial_max_slice_len(std::max<std::uint32_t>(1, max_detections))
                         .allocation_strategy(iox2::AllocationStrategy::Static)
                         .create();
    if (!publisher.has_value()) {
      throw std::runtime_error("Failed to create handpose publisher: " + service_name);
    }

    return std::move(publisher.value());
  }

  auto HandPoseTransport::timestamp_ns() -> std::uint64_t {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
  }

  IpcHandPoseStateMonitor::IpcHandPoseStateMonitor(const std::string& event_service_name,
                                                   const std::string& blackboard_service_name) :
      node_{create_node()},
      listener_{create_listener(node_, event_service_name)}, blackboard_service_{open_blackboard_service(
                                                                 node_, blackboard_service_name)},
      reader_{create_reader(blackboard_service_)}, cached_state_{signlang::state_machine::AppState::Normal} {
    cached_state_ = read_state_from_blackboard();
  }

  auto IpcHandPoseStateMonitor::is_enabled() const -> bool {
    return cached_state_ == signlang::state_machine::AppState::SignLanguageChat ||
        cached_state_ == signlang::state_machine::AppState::SignLanguageAi;
  }

  void IpcHandPoseStateMonitor::wait_for_state_change_blocking() {
    auto result = listener_.blocking_wait([](iox2::EventActivation /* event */) {});
    if (!result.has_value()) {
      throw std::runtime_error("Failed to wait for global app state change event in handpose detection");
    }

    cached_state_ = read_state_from_blackboard();
  }

  auto IpcHandPoseStateMonitor::try_wait_for_state_change() -> bool {
    bool event_received = false;

    auto result = listener_.try_wait([&event_received](iox2::EventActivation /* event */) { event_received = true; });
    if (!result.has_value()) {
      throw std::runtime_error("Failed to check for global app state change event in handpose detection");
    }

    if (event_received) {
      cached_state_ = read_state_from_blackboard();
    }

    return event_received;
  }

  auto IpcHandPoseStateMonitor::create_node() -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node = iox2::NodeBuilder().create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 IPC handpose state monitor node");
    }

    return std::move(node.value());
  }

  auto IpcHandPoseStateMonitor::create_listener(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                const std::string& service_name)
      -> iox2::Listener<iox2::ServiceType::Ipc> {
    auto service = node.service_builder(service_name_from_string(service_name)).event().open_or_create();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open or create iceoryx2 app state event service for handpose detection: " +
                               service_name);
    }

    auto listener = service.value().listener_builder().create();
    if (!listener.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state listener for handpose detection service: " +
                               service_name);
    }

    return std::move(listener.value());
  }

  auto IpcHandPoseStateMonitor::open_blackboard_service(const iox2::Node<iox2::ServiceType::Ipc>& node,
                                                        const std::string& service_name)
      -> iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> {
    auto service = node.service_builder(service_name_from_string(service_name))
                       .blackboard_opener<signlang::state_machine::AppStateKey>()
                       .open();
    if (!service.has_value()) {
      throw std::runtime_error("Failed to open iceoryx2 app state blackboard service for handpose detection: " +
                               service_name);
    }

    return std::move(service.value());
  }

  auto IpcHandPoseStateMonitor::create_reader(
      const iox2::PortFactoryBlackboard<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey>& service)
      -> iox2::Reader<iox2::ServiceType::Ipc, signlang::state_machine::AppStateKey> {
    auto reader = service.reader_builder().create();
    if (!reader.has_value()) {
      throw std::runtime_error("Failed to create iceoryx2 app state blackboard reader for handpose detection");
    }

    return std::move(reader.value());
  }

  auto IpcHandPoseStateMonitor::read_state_from_blackboard() -> signlang::state_machine::AppState {
    auto entry_result =
        reader_.entry<signlang::state_machine::AppState>(signlang::state_machine::default_app_state_key());
    if (!entry_result.has_value()) {
      return signlang::state_machine::AppState::Normal;
    }

    auto entry = std::move(entry_result.value());
    auto blackboard_value = entry.get();
    return *blackboard_value;
  }

} // namespace signlang::handpose_det

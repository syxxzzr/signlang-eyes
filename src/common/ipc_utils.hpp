#ifndef SIGNLANG_EYES_COMMON_IPC_UTILS_HPP
#define SIGNLANG_EYES_COMMON_IPC_UTILS_HPP

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace signlang::common::ipc {

  inline auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
    }
    return parsed_service_name.value();
  }

  inline auto create_ipc_node(const std::string& error_message) -> iox2::Node<iox2::ServiceType::Ipc> {
    iox2::set_log_level_from_env_or(iox2::LogLevel::Warn);

    auto node =
        iox2::NodeBuilder().signal_handling_mode(iox2::SignalHandlingMode::Disabled).create<iox2::ServiceType::Ipc>();
    if (!node.has_value()) {
      throw std::runtime_error(error_message);
    }
    return std::move(node.value());
  }

  template <typename PublishSubscribeService>
  auto subscriber_count(const PublishSubscribeService& service) -> std::uint64_t {
    return service.dynamic_config().number_of_subscribers();
  }

  template <typename PublishSubscribeService>
  auto has_subscribers(const PublishSubscribeService& service) -> bool {
    return subscriber_count(service) > 0;
  }

  template <typename RequestResponseService>
  auto server_count(const RequestResponseService& service) -> std::uint64_t {
    return service.dynamic_config().number_of_servers();
  }

  template <typename RequestResponseService>
  auto has_servers(const RequestResponseService& service) -> bool {
    return server_count(service) > 0;
  }

  template <typename Subscriber, typename Handler>
  auto receive_latest_sample(Subscriber& subscriber, const std::string& receive_error_message, Handler&& handler)
      -> bool {
    auto latest_sample = subscriber.receive();
    if (!latest_sample.has_value()) {
      throw std::runtime_error(receive_error_message);
    }

    if (!latest_sample.value().has_value()) {
      return false;
    }

    while (true) {
      auto next_sample = subscriber.receive();
      if (!next_sample.has_value()) {
        throw std::runtime_error(receive_error_message);
      }
      if (!next_sample.value().has_value()) {
        break;
      }
      latest_sample = std::move(next_sample);
    }

    handler(latest_sample.value().value().payload());
    return true;
  }

  template <typename Node, typename Client, typename Request, typename ResponseRequestId>
  auto send_request_and_wait_for_response(Node& node, Client& client, const Request& request,
                                          std::uint32_t expected_request_id, int max_wait_attempts,
                                          std::uint64_t response_wait_ms, const std::string& send_error_message,
                                          const std::string& no_server_error_message,
                                          const std::string& receive_error_message,
                                          const std::string& mismatched_response_error_message,
                                          const std::string& timeout_error_message,
                                          ResponseRequestId&& response_request_id) {
    auto send_result = client.send_copy(request);
    if (!send_result.has_value()) {
      throw std::runtime_error(send_error_message);
    }

    auto pending_response = std::move(send_result.value());
    if (pending_response.number_of_server_connections() == 0) {
      throw std::runtime_error(no_server_error_message);
    }

    for (auto attempt = 0; attempt < max_wait_attempts; ++attempt) {
      auto receive_result = pending_response.receive();
      if (!receive_result.has_value()) {
        throw std::runtime_error(receive_error_message);
      }

      if (receive_result.value().has_value()) {
        const auto response = receive_result.value().value().payload();
        if (response_request_id(response) != expected_request_id) {
          throw std::runtime_error(mismatched_response_error_message);
        }
        return response;
      }

      (void)node.wait(iox2::bb::Duration::from_millis(response_wait_ms));
    }

    throw std::runtime_error(timeout_error_message);
  }

} // namespace signlang::common::ipc

#endif // SIGNLANG_EYES_COMMON_IPC_UTILS_HPP

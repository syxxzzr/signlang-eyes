#ifndef SIGNLANG_EYES_COMMON_IPC_UTILS_HPP
#define SIGNLANG_EYES_COMMON_IPC_UTILS_HPP

#include "iox2/iceoryx2.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace signlang::common::ipc {

  inline auto service_name_from_string(const std::string& service_name) -> iox2::ServiceName {
    const auto parsed_service_name = iox2::ServiceName::create(service_name.c_str());
    if (!parsed_service_name.has_value()) {
      throw std::runtime_error("Invalid iceoryx2 service name: " + service_name);
    }
    return parsed_service_name.value();
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

} // namespace signlang::common::ipc

#endif // SIGNLANG_EYES_COMMON_IPC_UTILS_HPP

#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_SERIAL_TRANSPORT_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_SERIAL_TRANSPORT_HPP

#include "serial_protocol.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace signlang::peripheral_service {

  enum class SerialParity {
    None,
    Odd,
    Even,
  };

  enum class SerialFlowControl {
    None,
    Software,
    Hardware,
  };

  struct SerialOptions {
    std::string device = "/dev/ttyS3";
    std::uint32_t baud_rate = 115200;
    std::uint32_t data_bits = 8;
    std::uint32_t stop_bits = 1;
    SerialParity parity = SerialParity::None;
    SerialFlowControl flow_control = SerialFlowControl::None;
  };

  class SerialTransport {
  public:
    using ButtonCallback = std::function<void(ButtonEvent event)>;

    SerialTransport(SerialOptions options, ButtonCallback button_callback);
    ~SerialTransport();

    SerialTransport(const SerialTransport&) = delete;
    auto operator=(const SerialTransport&) -> SerialTransport& = delete;
    SerialTransport(SerialTransport&&) = delete;
    auto operator=(SerialTransport&&) -> SerialTransport& = delete;

    void start();
    void stop();
    void async_send(std::vector<std::uint8_t> frame);
    void async_send_many(std::vector<std::vector<std::uint8_t>> frames);

  private:
    void run();
    void open_serial();
    void read_next_byte();
    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred);
    void enqueue_write(std::vector<std::uint8_t> frame);
    void start_write_if_idle();
    void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred);

    SerialOptions options_;
    ButtonCallback button_callback_;
    boost::asio::io_context io_context_;
    boost::asio::serial_port serial_;
    std::uint8_t read_byte_{0};
    std::deque<std::vector<std::uint8_t>> write_queue_;
    std::mutex lifecycle_mutex_;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::atomic_bool stopping_{false};
  };

  [[nodiscard]] auto serial_parity_from_string(const std::string& value) -> SerialParity;
  [[nodiscard]] auto serial_flow_control_from_string(const std::string& value) -> SerialFlowControl;

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_SERIAL_TRANSPORT_HPP

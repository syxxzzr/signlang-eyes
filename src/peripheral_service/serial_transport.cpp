#include "serial_transport.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include <termios.h>

namespace signlang::peripheral_service {
  namespace asio = boost::asio;

  namespace {

    [[nodiscard]] auto to_boost_parity(SerialParity parity) -> asio::serial_port_base::parity {
      switch (parity) {
      case SerialParity::None:
        return asio::serial_port_base::parity{asio::serial_port_base::parity::none};
      case SerialParity::Odd:
        return asio::serial_port_base::parity{asio::serial_port_base::parity::odd};
      case SerialParity::Even:
        return asio::serial_port_base::parity{asio::serial_port_base::parity::even};
      }
      return asio::serial_port_base::parity{asio::serial_port_base::parity::none};
    }

    [[nodiscard]] auto to_boost_flow_control(SerialFlowControl flow_control)
        -> asio::serial_port_base::flow_control {
      switch (flow_control) {
      case SerialFlowControl::None:
        return asio::serial_port_base::flow_control{asio::serial_port_base::flow_control::none};
      case SerialFlowControl::Software:
        return asio::serial_port_base::flow_control{asio::serial_port_base::flow_control::software};
      case SerialFlowControl::Hardware:
        return asio::serial_port_base::flow_control{asio::serial_port_base::flow_control::hardware};
      }
      return asio::serial_port_base::flow_control{asio::serial_port_base::flow_control::none};
    }

    [[nodiscard]] auto to_boost_stop_bits(std::uint32_t stop_bits) -> asio::serial_port_base::stop_bits {
      if (stop_bits == 2U) {
        return asio::serial_port_base::stop_bits{asio::serial_port_base::stop_bits::two};
      }
      return asio::serial_port_base::stop_bits{asio::serial_port_base::stop_bits::one};
    }

    [[nodiscard]] auto lowercase(std::string value) -> std::string {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      return value;
    }

    void discard_serial_buffers(asio::serial_port& serial) {
      if (!serial.is_open()) {
        return;
      }

      const auto fd = serial.native_handle();
      if (fd >= 0) {
        (void)::tcflush(fd, TCIOFLUSH);
      }
    }

  } // namespace

  SerialTransport::SerialTransport(SerialOptions options, ButtonCallback button_callback) :
      options_{std::move(options)}, button_callback_{std::move(button_callback)}, serial_{io_context_} {}

  SerialTransport::~SerialTransport() { stop(); }

  void SerialTransport::start() {
    std::lock_guard lock{lifecycle_mutex_};
    if (running_) {
      return;
    }

    stopping_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread{&SerialTransport::run, this};
  }

  void SerialTransport::stop() {
    {
      std::lock_guard lock{lifecycle_mutex_};
      if (!running_ && !thread_.joinable()) {
        return;
      }
      stopping_.store(true, std::memory_order_release);
      asio::post(io_context_, [this] {
        boost::system::error_code ignored;
        write_queue_.clear();
        if (serial_.is_open()) {
          serial_.cancel(ignored);
          discard_serial_buffers(serial_);
          serial_.close(ignored);
        }
      });
    }

    if (thread_.joinable()) {
      thread_.join();
    }

    {
      std::lock_guard lock{lifecycle_mutex_};
      boost::system::error_code ignored;
      write_queue_.clear();
      if (serial_.is_open()) {
        serial_.cancel(ignored);
        discard_serial_buffers(serial_);
        serial_.close(ignored);
      }
      io_context_.restart();
      while (io_context_.poll_one() > 0) {
      }
    }
    running_.store(false, std::memory_order_release);
  }

  void SerialTransport::async_send(std::vector<std::uint8_t> frame) {
    if (stopping_.load(std::memory_order_acquire)) {
      return;
    }
    asio::post(io_context_, [this, frame = std::move(frame)]() mutable { enqueue_write(std::move(frame)); });
  }

  void SerialTransport::async_send_many(std::vector<std::vector<std::uint8_t>> frames) {
    if (stopping_.load(std::memory_order_acquire)) {
      return;
    }
    asio::post(io_context_, [this, frames = std::move(frames)]() mutable {
      if (stopping_.load(std::memory_order_acquire)) {
        return;
      }
      const auto was_idle = write_queue_.empty();
      for (auto& frame : frames) {
        if (!frame.empty()) {
          write_queue_.push_back(std::move(frame));
        }
      }
      if (was_idle) {
        start_write_if_idle();
      }
    });
  }

  void SerialTransport::run() {
    try {
      io_context_.restart();
      open_serial();
      read_next_byte();
      spdlog::info("peripheral serial opened: {} @ {}", options_.device, options_.baud_rate);
      io_context_.run();
    } catch (const std::exception& error) {
      if (!stopping_.load(std::memory_order_acquire)) {
        spdlog::error("peripheral serial transport stopped: {}", error.what());
      }
    }
    running_.store(false, std::memory_order_release);
  }

  void SerialTransport::open_serial() {
    serial_.open(options_.device);
    serial_.set_option(asio::serial_port_base::baud_rate{options_.baud_rate});
    serial_.set_option(asio::serial_port_base::character_size{options_.data_bits});
    serial_.set_option(to_boost_parity(options_.parity));
    serial_.set_option(to_boost_stop_bits(options_.stop_bits));
    serial_.set_option(to_boost_flow_control(options_.flow_control));
  }

  void SerialTransport::read_next_byte() {
    serial_.async_read_some(asio::buffer(&read_byte_, 1),
                            [this](const boost::system::error_code& error, std::size_t bytes_transferred) {
                              handle_read(error, bytes_transferred);
                            });
  }

  void SerialTransport::handle_read(const boost::system::error_code& error, std::size_t bytes_transferred) {
    if (error) {
      if (!stopping_.load(std::memory_order_acquire)) {
        spdlog::warn("peripheral serial read failed: {}", error.message());
      }
      return;
    }

    if (bytes_transferred == 1U) {
      const auto event = parse_button_event(read_byte_);
      if (event.has_value() && button_callback_) {
        button_callback_(*event);
      }
    }
    read_next_byte();
  }

  void SerialTransport::enqueue_write(std::vector<std::uint8_t> frame) {
    if (frame.empty() || stopping_.load(std::memory_order_acquire)) {
      return;
    }
    const auto was_idle = write_queue_.empty();
    write_queue_.push_back(std::move(frame));
    if (was_idle) {
      start_write_if_idle();
    }
  }

  void SerialTransport::start_write_if_idle() {
    if (write_queue_.empty() || !serial_.is_open()) {
      return;
    }
    asio::async_write(serial_, asio::buffer(write_queue_.front()),
                      [this](const boost::system::error_code& error, std::size_t bytes_transferred) {
                        handle_write(error, bytes_transferred);
                      });
  }

  void SerialTransport::handle_write(const boost::system::error_code& error, std::size_t /* bytes_transferred */) {
    if (error) {
      if (!stopping_.load(std::memory_order_acquire)) {
        spdlog::warn("peripheral serial write failed: {}", error.message());
      }
      write_queue_.clear();
      return;
    }

    if (!write_queue_.empty()) {
      write_queue_.pop_front();
    }
    start_write_if_idle();
  }

  auto serial_parity_from_string(const std::string& value) -> SerialParity {
    const auto normalized = lowercase(value);
    if (normalized == "none") {
      return SerialParity::None;
    }
    if (normalized == "odd") {
      return SerialParity::Odd;
    }
    if (normalized == "even") {
      return SerialParity::Even;
    }
    throw std::runtime_error("invalid serial parity: " + value);
  }

  auto serial_flow_control_from_string(const std::string& value) -> SerialFlowControl {
    const auto normalized = lowercase(value);
    if (normalized == "none") {
      return SerialFlowControl::None;
    }
    if (normalized == "software") {
      return SerialFlowControl::Software;
    }
    if (normalized == "hardware") {
      return SerialFlowControl::Hardware;
    }
    throw std::runtime_error("invalid serial flow control: " + value);
  }

} // namespace signlang::peripheral_service

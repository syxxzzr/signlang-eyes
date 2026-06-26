#include "protocol.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace signlang::signlang_manager {
  namespace {

    constexpr auto kMagic = std::array<std::uint8_t, 4>{'S', 'L', 'M', '1'};

    void append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }

    void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
      out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
      out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }

    void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
      out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
      out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
      out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
      out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    }

    auto read_u8(const std::vector<std::uint8_t>& bytes, std::size_t& offset) -> std::uint8_t {
      if (offset >= bytes.size()) {
        throw std::runtime_error("Protocol packet is truncated");
      }
      return bytes[offset++];
    }

    auto read_u16(const std::vector<std::uint8_t>& bytes, std::size_t& offset) -> std::uint16_t {
      if (offset + 2U > bytes.size()) {
        throw std::runtime_error("Protocol packet is truncated");
      }
      const auto value = static_cast<std::uint16_t>(bytes[offset]) |
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
      offset += 2U;
      return value;
    }

    auto read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& offset) -> std::uint32_t {
      if (offset + 4U > bytes.size()) {
        throw std::runtime_error("Protocol packet is truncated");
      }
      const auto value = static_cast<std::uint32_t>(bytes[offset]) |
          (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
          (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
      offset += 4U;
      return value;
    }

    auto is_valid_packet_type(std::uint8_t type) -> bool {
      return type == static_cast<std::uint8_t>(PacketType::Request) ||
          type == static_cast<std::uint8_t>(PacketType::Response) ||
          type == static_cast<std::uint8_t>(PacketType::Event) || type == static_cast<std::uint8_t>(PacketType::Stream);
    }

  } // namespace

  auto crc32(const std::uint8_t* data, std::size_t size) -> std::uint32_t {
    auto crc = std::uint32_t{0xFFFFFFFFU};
    for (std::size_t i = 0; i < size; ++i) {
      crc ^= data[i];
      for (auto bit = 0; bit < 8; ++bit) {
        const auto mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1U)));
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
      }
    }
    return ~crc;
  }

  auto encode_packet(const ProtocolPacket& packet) -> std::vector<std::uint8_t> {
    auto out = std::vector<std::uint8_t>{};
    out.reserve(static_cast<std::size_t>(kPacketHeaderSize) + packet.payload.size());

    out.insert(out.end(), kMagic.begin(), kMagic.end());
    append_u8(out, kProtocolVersion);
    append_u8(out, static_cast<std::uint8_t>(packet.type));
    append_u16(out, packet.command_id);
    append_u32(out, packet.request_id);
    append_u16(out, packet.flags);
    append_u16(out, kPacketHeaderSize);
    append_u32(out, static_cast<std::uint32_t>(packet.payload.size()));
    append_u32(out, crc32(packet.payload.data(), packet.payload.size()));
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());

    return out;
  }

  auto decode_packet(const std::vector<std::uint8_t>& bytes) -> ProtocolPacket {
    if (bytes.size() < kPacketHeaderSize) {
      throw std::runtime_error("Protocol packet is shorter than the header");
    }

    for (std::size_t i = 0; i < kMagic.size(); ++i) {
      if (bytes[i] != kMagic[i]) {
        throw std::runtime_error("Protocol packet magic is invalid");
      }
    }

    auto offset = std::size_t{kMagic.size()};
    const auto version = read_u8(bytes, offset);
    if (version != kProtocolVersion) {
      throw std::runtime_error("Unsupported protocol version: " + std::to_string(version));
    }

    const auto raw_type = read_u8(bytes, offset);
    if (!is_valid_packet_type(raw_type)) {
      throw std::runtime_error("Invalid protocol packet type");
    }

    auto packet = ProtocolPacket{};
    packet.type = static_cast<PacketType>(raw_type);
    packet.command_id = read_u16(bytes, offset);
    packet.request_id = read_u32(bytes, offset);
    packet.flags = read_u16(bytes, offset);

    const auto header_size = read_u16(bytes, offset);
    if (header_size != kPacketHeaderSize) {
      throw std::runtime_error("Unsupported protocol header size");
    }

    const auto payload_size = read_u32(bytes, offset);
    const auto expected_crc = read_u32(bytes, offset);
    if (bytes.size() != static_cast<std::size_t>(header_size) + payload_size) {
      throw std::runtime_error("Protocol packet payload size mismatch");
    }

    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(header_size), bytes.end());
    const auto actual_crc = crc32(packet.payload.data(), packet.payload.size());
    if (actual_crc != expected_crc) {
      throw std::runtime_error("Protocol packet payload CRC mismatch");
    }

    return packet;
  }

} // namespace signlang::signlang_manager

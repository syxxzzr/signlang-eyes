#include "signlang_manager/protocol.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace signlang::signlang_manager;

TEST_CASE("CRC32 matches the standard check value") {
  const auto bytes = std::vector<std::uint8_t>{'1', '2', '3', '4', '5', '6', '7', '8', '9'};

  CHECK(crc32(bytes.data(), bytes.size()) == 0xCBF43926U);
}

TEST_CASE("protocol packets round trip without losing fields") {
  const auto original = ProtocolPacket{
      PacketType::Response, static_cast<std::uint16_t>(CommandId::ListGestures), 0x12345678U, 0xA5A5U,
      {0x00U, 0x7FU, 0x80U, 0xFFU}};

  const auto encoded = encode_packet(original);
  const auto decoded = decode_packet(encoded);

  REQUIRE(encoded.size() == kPacketHeaderSize + original.payload.size());
  CHECK(decoded.type == original.type);
  CHECK(decoded.command_id == original.command_id);
  CHECK(decoded.request_id == original.request_id);
  CHECK(decoded.flags == original.flags);
  CHECK(decoded.payload == original.payload);
}

TEST_CASE("protocol decoder rejects malformed packets") {
  auto encoded = encode_packet(ProtocolPacket{PacketType::Event, 42U, 7U, 0U, {1U, 2U, 3U}});

  SECTION("bad magic") {
    encoded[0] = 'X';
    CHECK_THROWS_AS(decode_packet(encoded), std::runtime_error);
  }

  SECTION("unsupported version") {
    encoded[4] = 2U;
    CHECK_THROWS_AS(decode_packet(encoded), std::runtime_error);
  }

  SECTION("invalid packet type") {
    encoded[5] = 0U;
    CHECK_THROWS_AS(decode_packet(encoded), std::runtime_error);
  }

  SECTION("payload size mismatch") {
    encoded.pop_back();
    CHECK_THROWS_AS(decode_packet(encoded), std::runtime_error);
  }

  SECTION("payload corruption") {
    encoded.back() ^= 0xFFU;
    CHECK_THROWS_AS(decode_packet(encoded), std::runtime_error);
  }
}

TEST_CASE("protocol decoder rejects packets shorter than the header") {
  CHECK_THROWS_AS(decode_packet(std::vector<std::uint8_t>(kPacketHeaderSize - 1U)), std::runtime_error);
}

#include "manager_service.hpp"

#include "wire_handpose.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace signlang::signlang_manager {
  namespace {

    constexpr auto kStatusOk = std::uint16_t{0};
    constexpr auto kStatusBadRequest = std::uint16_t{1};
    constexpr auto kStatusNotFound = std::uint16_t{2};
    constexpr auto kStatusInternalError = std::uint16_t{3};
    constexpr auto kStatusUnsupported = std::uint16_t{4};

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

    auto read_u8(const std::vector<std::uint8_t>& payload, std::size_t& offset) -> std::uint8_t {
      if (offset >= payload.size()) {
        throw std::runtime_error("payload is truncated");
      }
      return payload[offset++];
    }

    auto read_u16(const std::vector<std::uint8_t>& payload, std::size_t& offset) -> std::uint16_t {
      if (offset + 2U > payload.size()) {
        throw std::runtime_error("payload is truncated");
      }
      const auto value = static_cast<std::uint16_t>(payload[offset]) |
          static_cast<std::uint16_t>(static_cast<std::uint16_t>(payload[offset + 1U]) << 8U);
      offset += 2U;
      return value;
    }

    auto read_u32(const std::vector<std::uint8_t>& payload, std::size_t& offset) -> std::uint32_t {
      if (offset + 4U > payload.size()) {
        throw std::runtime_error("payload is truncated");
      }
      const auto value = static_cast<std::uint32_t>(payload[offset]) |
          (static_cast<std::uint32_t>(payload[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(payload[offset + 2U]) << 16U) |
          (static_cast<std::uint32_t>(payload[offset + 3U]) << 24U);
      offset += 4U;
      return value;
    }

    auto read_string(const std::vector<std::uint8_t>& payload, std::size_t& offset) -> std::string {
      const auto size = read_u16(payload, offset);
      if (offset + size > payload.size()) {
        throw std::runtime_error("string payload is truncated");
      }
      auto value = std::string{reinterpret_cast<const char*>(payload.data() + offset), size};
      offset += size;
      return value;
    }

    void append_string(std::vector<std::uint8_t>& out, const std::string& value) {
      if (value.size() > UINT16_MAX) {
        throw std::runtime_error("string is too long for protocol payload");
      }
      append_u16(out, static_cast<std::uint16_t>(value.size()));
      out.insert(out.end(), value.begin(), value.end());
    }

    auto message_payload(const std::string& message) -> std::vector<std::uint8_t> {
      auto payload = std::vector<std::uint8_t>{};
      append_string(payload, message);
      return payload;
    }

    auto decode_uploaded_frames(const std::vector<std::uint8_t>& data) -> std::vector<WireHandposeFrame> {
      auto offset = std::size_t{0};
      const auto frame_count = read_u32(data, offset);
      auto frames = std::vector<WireHandposeFrame>{};
      frames.reserve(frame_count);
      for (std::uint32_t i = 0; i < frame_count; ++i) {
        const auto frame_size = read_u32(data, offset);
        if (offset + frame_size > data.size()) {
          throw std::runtime_error("uploaded handpose frame is truncated");
        }
        auto frame_payload = std::vector<std::uint8_t>{data.begin() + static_cast<std::ptrdiff_t>(offset),
                                                       data.begin() + static_cast<std::ptrdiff_t>(offset + frame_size)};
        offset += frame_size;
        frames.push_back(decode_wire_handpose_frame(frame_payload));
      }
      if (offset != data.size()) {
        throw std::runtime_error("uploaded handpose data has trailing bytes");
      }
      return frames;
    }

  } // namespace

  ManagerService::ManagerService(const ProgramOptions& options) :
      encoder_{options.model_path, options.npu_core_mask, options.motion_weight},
      database_{options.prototypes_path, encoder_.embedding_dim()},
      prototype_control_{options.signlang_control_service_name},
      min_keypoint_confidence_{options.min_keypoint_confidence}, upload_window_overlap_{options.upload_window_overlap},
      stream_fps_{options.stream_fps}, streaming_enabled_{options.enable_streaming_by_default} {
    database_.ensure_valid_empty_or_existing();
  }

  auto ManagerService::streaming_enabled() const -> bool { return streaming_enabled_.load(); }

  void ManagerService::set_streaming_enabled(bool enabled) { streaming_enabled_.store(enabled); }

  auto ManagerService::stream_interval_ns() const -> std::uint64_t {
    return 1000000000ULL / std::max<std::uint32_t>(1, stream_fps_);
  }

  auto ManagerService::build_stream_packet(const handpose_det::HandPoseFrameMetadata& metadata,
                                           const handpose_det::HandPoseDetection* detections,
                                           std::uint32_t detection_count) -> std::vector<std::uint8_t> {
    auto packet = ProtocolPacket{};
    packet.type = PacketType::Stream;
    packet.command_id = static_cast<std::uint16_t>(CommandId::HandposeFrame);
    packet.payload = encode_wire_handpose_frame(metadata, detections, detection_count, kMaxHandCount);
    return encode_packet(packet);
  }

  auto ManagerService::handle_packet_bytes(const std::vector<std::uint8_t>& bytes) -> std::vector<std::uint8_t> {
    try {
      const auto request = decode_packet(bytes);
      if (request.type != PacketType::Request) {
        return {};
      }
      return encode_packet(handle_request(request));
    } catch (const std::exception& error) {
      spdlog::warn("Failed to handle BLE command packet: {}", error.what());
      return {};
    }
  }

  auto ManagerService::handle_request(const ProtocolPacket& request) -> ProtocolPacket {
    try {
      switch (static_cast<CommandId>(request.command_id)) {
      case CommandId::GetCapabilities:
        return handle_get_status(request);
      case CommandId::SetStreamConfig:
        return handle_set_stream_config(request);
      case CommandId::ListGestures:
        return handle_list_gestures(request);
      case CommandId::AddGestureBegin:
        return handle_add_begin(request);
      case CommandId::AddGestureChunk:
        return handle_add_chunk(request);
      case CommandId::AddGestureCommit:
        return handle_add_commit(request);
      case CommandId::AddGestureAbort:
        return handle_add_abort(request);
      case CommandId::DeleteGesture:
        return handle_delete_gesture(request);
      case CommandId::GetStatus:
        return handle_get_status(request);
      default:
        return make_response(request, kStatusUnsupported, message_payload("unsupported"));
      }
    } catch (const std::exception& error) {
      return make_response(request, kStatusBadRequest, message_payload(error.what()));
    }
  }

  auto ManagerService::make_response(const ProtocolPacket& request, std::uint16_t status,
                                     const std::vector<std::uint8_t>& payload) const -> ProtocolPacket {
    auto response_payload = std::vector<std::uint8_t>{};
    append_u16(response_payload, status);
    response_payload.insert(response_payload.end(), payload.begin(), payload.end());

    return ProtocolPacket{
        .type = PacketType::Response,
        .command_id = request.command_id,
        .request_id = request.request_id,
        .flags = 0,
        .payload = std::move(response_payload),
    };
  }

  auto ManagerService::handle_get_status(const ProtocolPacket& request) -> ProtocolPacket {
    auto payload = std::vector<std::uint8_t>{};
    append_u16(payload, kProtocolVersion);
    append_u16(payload, 0);
    append_u32(payload, encoder_.sequence_length());
    append_u32(payload, encoder_.embedding_dim());
    append_u32(payload, stream_fps_);
    append_u8(payload, streaming_enabled_.load() ? 1U : 0U);
    return make_response(request, kStatusOk, payload);
  }

  auto ManagerService::handle_set_stream_config(const ProtocolPacket& request) -> ProtocolPacket {
    auto offset = std::size_t{0};
    const auto enabled = read_u8(request.payload, offset) != 0;
    set_streaming_enabled(enabled);
    return make_response(request, kStatusOk);
  }

  auto ManagerService::handle_list_gestures(const ProtocolPacket& request) -> ProtocolPacket {
    const auto gestures = database_.list_gestures();
    auto payload = std::vector<std::uint8_t>{};
    append_u16(payload, static_cast<std::uint16_t>(gestures.size()));
    for (const auto& gesture : gestures) {
      append_u32(payload, gesture.id);
      append_u8(payload, gesture.enabled ? 1U : 0U);
      append_u32(payload, gesture.sample_count);
      append_string(payload, gesture.name);
    }
    return make_response(request, kStatusOk, payload);
  }

  auto ManagerService::handle_delete_gesture(const ProtocolPacket& request) -> ProtocolPacket {
    auto offset = std::size_t{0};
    const auto mode = read_u8(request.payload, offset);
    auto deleted = false;
    if (mode == 1) {
      deleted = database_.delete_gesture(read_u32(request.payload, offset));
    } else if (mode == 2) {
      deleted = database_.delete_gesture(read_string(request.payload, offset));
    } else {
      throw std::runtime_error("delete mode must be 1=id or 2=name");
    }

    if (!deleted) {
      return make_response(request, kStatusNotFound, message_payload("gesture not found"));
    }
    prototype_control_.request_reload();
    return make_response(request, kStatusOk);
  }

  auto ManagerService::handle_add_begin(const ProtocolPacket& request) -> ProtocolPacket {
    auto offset = std::size_t{0};
    const auto transfer_id = read_u32(request.payload, offset);
    const auto total_size = read_u32(request.payload, offset);
    const auto replace_existing = read_u8(request.payload, offset) != 0;
    const auto gesture_name = read_string(request.payload, offset);
    if (gesture_name.empty()) {
      throw std::runtime_error("gesture name must not be empty");
    }
    if (total_size == 0) {
      throw std::runtime_error("upload total size must be greater than zero");
    }

    upload_ = UploadSession{
        .transfer_id = transfer_id,
        .gesture_name = gesture_name,
        .replace_existing = replace_existing,
        .total_size = total_size,
        .data = std::vector<std::uint8_t>(total_size),
        .received = std::vector<std::uint8_t>(total_size, 0),
    };
    return make_response(request, kStatusOk);
  }

  auto ManagerService::handle_add_chunk(const ProtocolPacket& request) -> ProtocolPacket {
    if (!upload_.has_value()) {
      throw std::runtime_error("no upload session is active");
    }

    auto offset = std::size_t{0};
    const auto transfer_id = read_u32(request.payload, offset);
    const auto chunk_offset = read_u32(request.payload, offset);
    const auto chunk_size = read_u32(request.payload, offset);
    if (transfer_id != upload_->transfer_id) {
      throw std::runtime_error("upload transfer id mismatch");
    }
    if (offset + chunk_size != request.payload.size()) {
      throw std::runtime_error("upload chunk size mismatch");
    }
    if (chunk_offset + chunk_size > upload_->data.size()) {
      throw std::runtime_error("upload chunk exceeds declared total size");
    }

    std::copy_n(request.payload.data() + offset, chunk_size, upload_->data.data() + chunk_offset);
    std::fill_n(upload_->received.data() + chunk_offset, chunk_size, 1);
    return make_response(request, kStatusOk);
  }

  auto ManagerService::handle_add_commit(const ProtocolPacket& request) -> ProtocolPacket {
    if (!upload_.has_value()) {
      throw std::runtime_error("no upload session is active");
    }

    auto offset = std::size_t{0};
    const auto transfer_id = read_u32(request.payload, offset);
    if (transfer_id != upload_->transfer_id) {
      throw std::runtime_error("upload transfer id mismatch");
    }
    if (!std::all_of(upload_->received.begin(), upload_->received.end(),
                     [](std::uint8_t received) { return received != 0; })) {
      throw std::runtime_error("upload has missing chunks");
    }

    const auto gesture_id = encode_uploaded_gesture(*upload_);
    upload_.reset();
    prototype_control_.request_reload();

    auto payload = std::vector<std::uint8_t>{};
    append_u32(payload, gesture_id);
    return make_response(request, kStatusOk, payload);
  }

  auto ManagerService::handle_add_abort(const ProtocolPacket& request) -> ProtocolPacket {
    upload_.reset();
    return make_response(request, kStatusOk);
  }

  auto ManagerService::encode_uploaded_gesture(const UploadSession& session) -> std::uint32_t {
    const auto frames = decode_uploaded_frames(session.data);
    auto extractor = GestureFeatureExtractor{min_keypoint_confidence_};
    auto features = std::vector<FeatureVector>{};
    features.reserve(frames.size());
    for (const auto& frame : frames) {
      if (auto feature = extractor.extract(frame.metadata, frame.detections.data(),
                                           static_cast<std::uint32_t>(frame.detections.size()))) {
        features.push_back(*feature);
      }
    }

    const auto sequence_length = encoder_.sequence_length();
    if (features.size() < sequence_length) {
      throw std::runtime_error("uploaded gesture does not contain enough valid handpose frames");
    }

    const auto hop = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(static_cast<float>(sequence_length) * (1.0F - upload_window_overlap_)));
    auto gesture_id = std::uint32_t{0};
    auto replace_next = session.replace_existing;
    for (std::size_t start = 0; start + sequence_length <= features.size(); start += hop) {
      auto window = std::vector<FeatureVector>{features.begin() + static_cast<std::ptrdiff_t>(start),
                                               features.begin() + static_cast<std::ptrdiff_t>(start + sequence_length)};
      const auto encoded = encoder_.encode(window);
      gesture_id = database_.add_gesture_sample(session.gesture_name, encoded, replace_next);
      replace_next = false;
    }

    spdlog::info("Stored uploaded gesture '{}' as id {}", session.gesture_name, gesture_id);
    return gesture_id;
  }

} // namespace signlang::signlang_manager

#include "manager_service.hpp"

#include "common/fixed_string.hpp"
#include "wire_handpose.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_manager {
  namespace {

    constexpr auto kStatusOk = std::uint16_t{0};
    constexpr auto kStatusBadRequest = std::uint16_t{1};
    constexpr auto kStatusNotFound = std::uint16_t{2};
    constexpr auto kStatusFailed = std::uint16_t{3};
    constexpr auto kStatusUnsupported = std::uint16_t{4};
    constexpr auto kCurrentStreamPayloadVersion = std::uint8_t{3};

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

    void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
      for (auto shift = 0U; shift < 64U; shift += 8U) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
      }
    }

    void append_f32(std::vector<std::uint8_t>& out, float value) {
      auto bits = std::uint32_t{0};
      std::memcpy(&bits, &value, sizeof(bits));
      append_u32(out, bits);
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

    auto encode_stream_payload(const handpose_det::HandPoseFrameMetadata& metadata,
                               const handpose_det::HandPoseDetection* detections, std::uint32_t detection_count,
                               const signlang_det::SignlangResult* signlang_result) -> std::vector<std::uint8_t> {
      const auto handpose_payload =
          encode_wire_handpose_frame(metadata, detections, detection_count, signlang_det::kMaxHandCount);
      auto out = std::vector<std::uint8_t>{};
      out.reserve(handpose_payload.size() + 96U);

      append_u8(out, kCurrentStreamPayloadVersion);
      append_u8(out, signlang_result != nullptr ? 1U : 0U);
      append_u16(out, 0);
      append_u32(out, static_cast<std::uint32_t>(handpose_payload.size()));
      out.insert(out.end(), handpose_payload.begin(), handpose_payload.end());

      if (signlang_result != nullptr) {
        append_u64(out, signlang_result->sequence_number);
        append_u64(out, signlang_result->timestamp_ns);
        append_u8(out, signlang_result->recognized ? 1U : 0U);
        append_u32(out, signlang_result->gesture_id);
        append_f32(out, signlang_result->distance);
        append_string(out, common::fixed_string_to_string(signlang_result->gesture_name));
      }

      return out;
    }

    auto response_message(const signlang_det::GestureManagementResponse& response) -> std::string {
      return common::fixed_string_to_string(response.message);
    }

    auto status_to_ble(signlang_det::GestureManagementStatus status) -> std::uint16_t {
      switch (status) {
      case signlang_det::GestureManagementStatus::Ok:
        return kStatusOk;
      case signlang_det::GestureManagementStatus::BadRequest:
        return kStatusBadRequest;
      case signlang_det::GestureManagementStatus::NotFound:
        return kStatusNotFound;
      case signlang_det::GestureManagementStatus::UnsupportedCommand:
        return kStatusUnsupported;
      case signlang_det::GestureManagementStatus::Failed:
        return kStatusFailed;
      }
      return kStatusFailed;
    }

  } // namespace

  ManagerService::ManagerService(const ProgramOptions& options) :
      gesture_management_{options.gesture_management_service_name}, stream_fps_{options.stream_fps},
      max_upload_bytes_{options.max_upload_bytes}, streaming_enabled_{options.enable_streaming_by_default} {}

  auto ManagerService::streaming_enabled() const -> bool { return streaming_enabled_.load(); }

  void ManagerService::set_streaming_enabled(bool enabled) { streaming_enabled_.store(enabled); }

  auto ManagerService::stream_interval_ns() const -> std::uint64_t {
    return 1000000000ULL / std::max<std::uint32_t>(1, stream_fps_);
  }

  auto ManagerService::build_stream_packet(const handpose_det::HandPoseFrameMetadata& metadata,
                                           const handpose_det::HandPoseDetection* detections,
                                           std::uint32_t detection_count,
                                           const signlang_det::SignlangResult* signlang_result)
      -> std::vector<std::uint8_t> {
    auto packet = ProtocolPacket{};
    packet.type = PacketType::Stream;
    packet.command_id = static_cast<std::uint16_t>(CommandId::HandposeFrame);
    packet.payload = encode_stream_payload(metadata, detections, detection_count, signlang_result);
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

    return ProtocolPacket{PacketType::Response, request.command_id, request.request_id, 0, std::move(response_payload)};
  }

  auto ManagerService::handle_get_status(const ProtocolPacket& request) -> ProtocolPacket {
    const auto management_response =
        send_management_request(make_management_request(signlang_det::GestureManagementCommand::GetStatus));
    auto payload = std::vector<std::uint8_t>{};
    append_u16(payload, kProtocolVersion);
    append_u16(payload, 0);
    append_u32(payload, management_response.sequence_length);
    append_u32(payload, management_response.embedding_dim);
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
    const auto management_response =
        send_management_request(make_management_request(signlang_det::GestureManagementCommand::ListGestures));
    if (management_response.status != signlang_det::GestureManagementStatus::Ok) {
      return make_response(request, status_to_ble(management_response.status),
                           message_payload(response_message(management_response)));
    }

    auto payload = std::vector<std::uint8_t>{};
    append_u16(payload, static_cast<std::uint16_t>(management_response.gesture_count));
    for (std::uint32_t index = 0; index < management_response.gesture_count; ++index) {
      const auto& gesture = management_response.gestures[index];
      append_u32(payload, gesture.id);
      append_u8(payload, gesture.enabled ? 1U : 0U);
      append_u32(payload, gesture.sample_count);
      append_u8(payload, gesture.calibrated ? 1U : 0U);
      append_string(payload, common::fixed_string_to_string(gesture.name));
    }
    return make_response(request, kStatusOk, payload);
  }

  auto ManagerService::handle_delete_gesture(const ProtocolPacket& request) -> ProtocolPacket {
    auto offset = std::size_t{0};
    const auto mode = read_u8(request.payload, offset);
    auto management_request = signlang_det::GestureManagementRequest{};
    if (mode == 1) {
      management_request = make_management_request(signlang_det::GestureManagementCommand::DeleteGestureById);
      management_request.gesture_id = read_u32(request.payload, offset);
    } else if (mode == 2) {
      management_request = make_management_request(signlang_det::GestureManagementCommand::DeleteGestureByName);
      common::copy_fixed_string(read_string(request.payload, offset), management_request.gesture_name);
    } else {
      throw std::runtime_error("delete mode must be 1=id or 2=name");
    }

    const auto management_response = send_management_request(management_request);
    if (management_response.status != signlang_det::GestureManagementStatus::Ok) {
      return make_response(request, status_to_ble(management_response.status),
                           message_payload(response_message(management_response)));
    }
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
    if (total_size > max_upload_bytes_) {
      throw std::runtime_error("upload total size exceeds configured maximum of " + std::to_string(max_upload_bytes_) +
                               " bytes");
    }

    upload_ = UploadSession{transfer_id, gesture_name, replace_existing, total_size, std::vector<std::uint8_t>(total_size),
                            std::vector<std::uint8_t>(total_size, 0)};
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
    if (chunk_offset > upload_->data.size() || chunk_size > upload_->data.size() - chunk_offset) {
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

    const auto frames = decode_uploaded_frames(upload_->data);
    if (frames.empty()) {
      throw std::runtime_error("uploaded gesture does not contain any handpose frames");
    }
    if (frames.size() > UINT32_MAX) {
      throw std::runtime_error("uploaded gesture frame count exceeds IPC range");
    }

    auto begin_request = make_management_request(signlang_det::GestureManagementCommand::AddGestureBegin);
    begin_request.transfer_id = transfer_id;
    begin_request.frame_count = static_cast<std::uint32_t>(frames.size());
    begin_request.replace_existing = upload_->replace_existing;
    common::copy_fixed_string(upload_->gesture_name, begin_request.gesture_name);
    auto management_response = send_management_request(begin_request);
    if (management_response.status != signlang_det::GestureManagementStatus::Ok) {
      return make_response(request, status_to_ble(management_response.status),
                           message_payload(response_message(management_response)));
    }

    for (std::uint32_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
      const auto& frame = frames[frame_index];
      if (frame.detections.size() > signlang_det::kMaxHandCount) {
        throw std::runtime_error("uploaded handpose frame exceeds supported hand count");
      }

      auto frame_request = make_management_request(signlang_det::GestureManagementCommand::AddGestureChunk);
      frame_request.transfer_id = transfer_id;
      frame_request.frame_index = frame_index;
      frame_request.detection_count = static_cast<std::uint32_t>(frame.detections.size());
      frame_request.frame_metadata = frame.metadata;
      std::copy(frame.detections.begin(), frame.detections.end(), frame_request.detections.begin());
      management_response = send_management_request(frame_request);
      if (management_response.status != signlang_det::GestureManagementStatus::Ok) {
        auto abort_request = make_management_request(signlang_det::GestureManagementCommand::AddGestureAbort);
        abort_request.transfer_id = transfer_id;
        (void)send_management_request(abort_request);
        return make_response(request, status_to_ble(management_response.status),
                             message_payload(response_message(management_response)));
      }
    }

    auto commit_request = make_management_request(signlang_det::GestureManagementCommand::AddGestureCommit);
    commit_request.transfer_id = transfer_id;
    management_response = send_management_request(commit_request);
    if (management_response.status != signlang_det::GestureManagementStatus::Ok) {
      return make_response(request, status_to_ble(management_response.status),
                           message_payload(response_message(management_response)));
    }
    const auto gesture_id = management_response.gesture_id;
    upload_.reset();

    auto payload = std::vector<std::uint8_t>{};
    append_u32(payload, gesture_id);
    return make_response(request, kStatusOk, payload);
  }

  auto ManagerService::handle_add_abort(const ProtocolPacket& request) -> ProtocolPacket {
    upload_.reset();
    return make_response(request, kStatusOk);
  }

  auto ManagerService::make_management_request(signlang_det::GestureManagementCommand command)
      -> signlang_det::GestureManagementRequest {
    static auto request_id = std::uint32_t{0};
    auto request = signlang_det::GestureManagementRequest{};
    request.command = command;
    request.request_id = request_id++;
    return request;
  }

  auto ManagerService::send_management_request(signlang_det::GestureManagementRequest request)
      -> signlang_det::GestureManagementResponse {
    return gesture_management_.request(request);
  }

} // namespace signlang::signlang_manager

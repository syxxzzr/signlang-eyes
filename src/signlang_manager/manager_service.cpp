#include "manager_service.hpp"

#include "wire_handpose.hpp"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace signlang::signlang_manager {
  namespace {

    constexpr auto kStatusOk = std::uint16_t{0};
    constexpr auto kStatusBadRequest = std::uint16_t{1};
    constexpr auto kStatusNotFound = std::uint16_t{2};
    constexpr auto kStatusInternalError = std::uint16_t{3};
    constexpr auto kStatusUnsupported = std::uint16_t{4};
    constexpr auto kStreamPayloadVersionWithSignlang = std::uint8_t{2};
    constexpr auto kMaxRepresentativeSamples = std::size_t{3};

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

    auto fixed_cstr(const std::array<char, signlang_det::kMaxGestureNameLength>& value) -> std::string {
      const auto end = std::find(value.begin(), value.end(), '\0');
      return std::string{value.begin(), end};
    }

    auto frame_distance(const std::vector<float>& lhs, const std::vector<float>& rhs) -> float {
      if (lhs.size() != rhs.size() || lhs.empty()) {
        return std::numeric_limits<float>::infinity();
      }

      auto sum_sq_diff = 0.0F;
      for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto diff = lhs[i] - rhs[i];
        sum_sq_diff += diff * diff;
      }
      return std::sqrt(sum_sq_diff / static_cast<float>(lhs.size()));
    }

    auto dtw_distance(const EncodedSequence& lhs, const EncodedSequence& rhs) -> float {
      if (lhs.empty() || rhs.empty()) {
        return std::numeric_limits<float>::infinity();
      }

      const auto lhs_length = lhs.size();
      const auto rhs_length = rhs.size();
      auto prev_cost = std::vector<float>(rhs_length + 1U, std::numeric_limits<float>::infinity());
      auto prev_steps = std::vector<std::uint32_t>(rhs_length + 1U, 0);
      prev_cost[0] = 0.0F;

      for (std::size_t i = 1; i <= lhs_length; ++i) {
        auto curr_cost = std::vector<float>(rhs_length + 1U, std::numeric_limits<float>::infinity());
        auto curr_steps = std::vector<std::uint32_t>(rhs_length + 1U, 0);

        for (std::size_t j = 1; j <= rhs_length; ++j) {
          const auto candidates = std::array<std::pair<float, std::uint32_t>, 3>{{
              {prev_cost[j], prev_steps[j]},
              {curr_cost[j - 1U], curr_steps[j - 1U]},
              {prev_cost[j - 1U], prev_steps[j - 1U]},
          }};
          const auto best =
              *std::min_element(candidates.begin(), candidates.end(),
                                [](const auto& left, const auto& right) { return left.first < right.first; });

          curr_cost[j] = best.first + frame_distance(lhs[i - 1U], rhs[j - 1U]);
          curr_steps[j] = best.second + 1U;
        }

        prev_cost = std::move(curr_cost);
        prev_steps = std::move(curr_steps);
      }

      if (!std::isfinite(prev_cost[rhs_length]) || prev_steps[rhs_length] == 0) {
        return std::numeric_limits<float>::infinity();
      }
      return prev_cost[rhs_length] / static_cast<float>(prev_steps[rhs_length]);
    }

    auto select_representative_samples(const std::vector<EncodedSequence>& samples, std::size_t max_samples)
        -> std::vector<EncodedSequence> {
      if (samples.size() <= max_samples) {
        return samples;
      }

      const auto count = samples.size();
      auto distances = std::vector<std::vector<float>>(count, std::vector<float>(count, 0.0F));
      for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1U; j < count; ++j) {
          const auto distance = dtw_distance(samples[i], samples[j]);
          distances[i][j] = distance;
          distances[j][i] = distance;
        }
      }

      auto cluster_cost = [&](const std::vector<std::size_t>& medoids) {
        auto cost = 0.0F;
        for (std::size_t sample_index = 0; sample_index < count; ++sample_index) {
          auto nearest = std::numeric_limits<float>::infinity();
          for (const auto medoid : medoids) {
            nearest = std::min(nearest, distances[sample_index][medoid]);
          }
          cost += nearest;
        }
        return cost;
      };

      auto medoids = std::vector<std::size_t>{};
      while (medoids.size() < max_samples) {
        auto best_index = std::size_t{0};
        auto best_cost = std::numeric_limits<float>::infinity();
        for (std::size_t candidate = 0; candidate < count; ++candidate) {
          if (std::find(medoids.begin(), medoids.end(), candidate) != medoids.end()) {
            continue;
          }
          auto trial = medoids;
          trial.push_back(candidate);
          const auto cost = cluster_cost(trial);
          if (cost < best_cost) {
            best_cost = cost;
            best_index = candidate;
          }
        }
        medoids.push_back(best_index);
      }

      for (auto iteration = 0; iteration < 4; ++iteration) {
        auto clusters = std::vector<std::vector<std::size_t>>(medoids.size());
        for (std::size_t sample_index = 0; sample_index < count; ++sample_index) {
          auto best_cluster = std::size_t{0};
          auto best_distance = std::numeric_limits<float>::infinity();
          for (std::size_t medoid_index = 0; medoid_index < medoids.size(); ++medoid_index) {
            const auto distance = distances[sample_index][medoids[medoid_index]];
            if (distance < best_distance) {
              best_distance = distance;
              best_cluster = medoid_index;
            }
          }
          clusters[best_cluster].push_back(sample_index);
        }

        auto changed = false;
        for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
          const auto& cluster = clusters[cluster_index];
          if (cluster.empty()) {
            continue;
          }

          auto best_member = medoids[cluster_index];
          auto best_cost = std::numeric_limits<float>::infinity();
          for (const auto candidate : cluster) {
            auto cost = 0.0F;
            for (const auto member : cluster) {
              cost += distances[candidate][member];
            }
            if (cost < best_cost) {
              best_cost = cost;
              best_member = candidate;
            }
          }

          if (best_member != medoids[cluster_index]) {
            medoids[cluster_index] = best_member;
            changed = true;
          }
        }

        if (!changed) {
          break;
        }
      }

      std::sort(medoids.begin(), medoids.end());
      auto representatives = std::vector<EncodedSequence>{};
      representatives.reserve(medoids.size());
      for (const auto medoid : medoids) {
        representatives.push_back(samples[medoid]);
      }
      return representatives;
    }

    auto encode_stream_payload(const handpose_det::HandPoseFrameMetadata& metadata,
                               const handpose_det::HandPoseDetection* detections, std::uint32_t detection_count,
                               const signlang_det::SignlangResult* signlang_result) -> std::vector<std::uint8_t> {
      const auto handpose_payload = encode_wire_handpose_frame(metadata, detections, detection_count, kMaxHandCount);
      auto out = std::vector<std::uint8_t>{};
      out.reserve(handpose_payload.size() + 96U);

      append_u8(out, kStreamPayloadVersionWithSignlang);
      append_u8(out, signlang_result != nullptr ? 1U : 0U);
      append_u16(out, 0);
      append_u32(out, static_cast<std::uint32_t>(handpose_payload.size()));
      out.insert(out.end(), handpose_payload.begin(), handpose_payload.end());

      if (signlang_result != nullptr) {
        append_u64(out, signlang_result->sequence_number);
        append_u64(out, signlang_result->timestamp_ns);
        append_u8(out, signlang_result->recognized ? 1U : 0U);
        append_u32(out, signlang_result->gesture_id);
        append_f32(out, signlang_result->confidence);
        append_f32(out, signlang_result->second_confidence);
        append_f32(out, signlang_result->confidence_margin);
        append_f32(out, signlang_result->distance);
        append_string(out, fixed_cstr(signlang_result->gesture_name));
      }

      return out;
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
      stream_fps_{options.stream_fps}, max_upload_bytes_{options.max_upload_bytes},
      streaming_enabled_{options.enable_streaming_by_default} {
    database_.ensure_valid_empty_or_existing();
  }

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
    if (total_size > max_upload_bytes_) {
      throw std::runtime_error("upload total size exceeds configured maximum of " + std::to_string(max_upload_bytes_) +
                               " bytes");
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
    auto uploaded_samples = std::vector<EncodedSequence>{};
    for (std::size_t start = 0; start + sequence_length <= features.size(); start += hop) {
      auto window = std::vector<FeatureVector>{features.begin() + static_cast<std::ptrdiff_t>(start),
                                               features.begin() + static_cast<std::ptrdiff_t>(start + sequence_length)};
      uploaded_samples.push_back(encoder_.encode(window));
    }

    auto candidate_samples = session.replace_existing ? std::vector<EncodedSequence>{}
                                                      : database_.load_gesture_samples(session.gesture_name);
    const auto existing_sample_count = candidate_samples.size();
    candidate_samples.insert(candidate_samples.end(), uploaded_samples.begin(), uploaded_samples.end());

    const auto representative_samples = select_representative_samples(candidate_samples, kMaxRepresentativeSamples);
    const auto gesture_id = database_.replace_gesture_samples(session.gesture_name, representative_samples);
    spdlog::info(
        "Stored uploaded gesture '{}' as id {}; uploaded_windows={}, existing_samples={}, stored_representatives={}",
        session.gesture_name, gesture_id, uploaded_samples.size(), existing_sample_count,
        representative_samples.size());
    return gesture_id;
  }

} // namespace signlang::signlang_manager

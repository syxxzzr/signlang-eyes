#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_MANAGER_SERVICE_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_MANAGER_SERVICE_HPP

#include "gesture_encoder.hpp"
#include "gesture_feature_extractor.hpp"
#include "iceoryx_gateway.hpp"
#include "program_options.hpp"
#include "protocol.hpp"
#include "prototype_database.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace signlang::signlang_manager {

  class ManagerService {
  public:
    explicit ManagerService(const ProgramOptions& options);

    auto handle_packet_bytes(const std::vector<std::uint8_t>& bytes) -> std::vector<std::uint8_t>;
    auto build_stream_packet(const handpose_det::HandPoseFrameMetadata& metadata,
                             const handpose_det::HandPoseDetection* detections, std::uint32_t detection_count)
        -> std::vector<std::uint8_t>;

    auto streaming_enabled() const -> bool;
    auto stream_interval_ns() const -> std::uint64_t;
    void set_streaming_enabled(bool enabled);

  private:
    struct UploadSession {
      std::uint32_t transfer_id;
      std::string gesture_name;
      bool replace_existing;
      std::uint32_t total_size;
      std::vector<std::uint8_t> data;
      std::vector<std::uint8_t> received;
    };

    auto handle_request(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_list_gestures(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_set_stream_config(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_delete_gesture(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_add_begin(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_add_chunk(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_add_commit(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_add_abort(const ProtocolPacket& request) -> ProtocolPacket;
    auto handle_get_status(const ProtocolPacket& request) -> ProtocolPacket;

    auto encode_uploaded_gesture(const UploadSession& session) -> std::uint32_t;
    auto make_response(const ProtocolPacket& request, std::uint16_t status,
                       const std::vector<std::uint8_t>& payload = {}) const -> ProtocolPacket;

    GestureEncoder encoder_;
    PrototypeDatabase database_;
    IpcPrototypeControlClient prototype_control_;
    float min_keypoint_confidence_;
    float upload_window_overlap_;
    std::uint32_t stream_fps_;
    std::atomic_bool streaming_enabled_;
    std::optional<UploadSession> upload_;
  };

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_MANAGER_SERVICE_HPP

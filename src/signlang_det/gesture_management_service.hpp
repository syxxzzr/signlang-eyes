#ifndef SIGNLANG_EYES_SIGNLANG_DET_GESTURE_MANAGEMENT_SERVICE_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_GESTURE_MANAGEMENT_SERVICE_HPP

#include "gesture_management.hpp"
#include "program_options.hpp"
#include "prototype_database.hpp"
#include "signlang_model.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace signlang::signlang_det {

  class GestureManagementService {
  public:
    GestureManagementService(const ProgramOptions& options, SignlangModel& model, std::mutex& model_mutex);

    [[nodiscard]] auto handle_request(const GestureManagementRequest& request) -> GestureManagementResponse;

  private:
    struct UploadSession {
      std::uint32_t transfer_id;
      std::string gesture_name;
      bool replace_existing;
      std::uint32_t frame_count;
      std::vector<handpose_det::HandPoseFrameMetadata> metadata;
      std::vector<std::array<handpose_det::HandPoseDetection, kMaxHandCount>> detections;
      std::vector<std::uint32_t> detection_counts;
      std::vector<std::uint8_t> received;
    };

    [[nodiscard]] auto handle_list_gestures(const GestureManagementRequest& request) -> GestureManagementResponse;
    [[nodiscard]] auto handle_delete_gesture(const GestureManagementRequest& request) -> GestureManagementResponse;
    [[nodiscard]] auto handle_add_begin(const GestureManagementRequest& request) -> GestureManagementResponse;
    [[nodiscard]] auto handle_add_chunk(const GestureManagementRequest& request) -> GestureManagementResponse;
    [[nodiscard]] auto handle_add_commit(const GestureManagementRequest& request) -> GestureManagementResponse;
    [[nodiscard]] auto handle_add_abort(const GestureManagementRequest& request) -> GestureManagementResponse;
    [[nodiscard]] auto handle_get_status(const GestureManagementRequest& request) -> GestureManagementResponse;

    [[nodiscard]] auto encode_uploaded_gesture(const UploadSession& session) -> std::uint32_t;
    [[nodiscard]] auto make_response(const GestureManagementRequest& request, GestureManagementStatus status,
                                     const std::string& message = {}) const -> GestureManagementResponse;
    void reload_model_prototypes();

    const ProgramOptions& options_;
    SignlangModel& model_;
    std::mutex& model_mutex_;
    PrototypeDatabase database_;
    std::optional<UploadSession> upload_;
  };

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_GESTURE_MANAGEMENT_SERVICE_HPP

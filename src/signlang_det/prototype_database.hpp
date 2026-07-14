#ifndef SIGNLANG_EYES_SIGNLANG_DET_PROTOTYPE_DATABASE_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_PROTOTYPE_DATABASE_HPP

#include "signlang_model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace signlang::signlang_det {

  struct GestureInfo {
    std::uint32_t id;
    std::string name;
    bool enabled;
    std::uint32_t sample_count;
    CalibrationStatus calibration;
  };

  class PrototypeDatabase {
  public:
    explicit PrototypeDatabase(std::string path);
    void ensure_valid_empty_or_existing();
    [[nodiscard]] auto load_store() const -> PrototypeStore;
    [[nodiscard]] auto list_gestures() const -> std::vector<GestureInfo>;
    [[nodiscard]] auto load_gesture_samples(const std::string& gesture_name) const -> std::vector<GesturePrototype>;
    [[nodiscard]] auto replace_gesture_samples(const std::string& gesture_name,
                                               const std::vector<GesturePrototype>& samples,
                                               float dtw_threshold, float coarse_threshold,
                                               CalibrationStatus calibration) -> std::uint32_t;
    [[nodiscard]] auto delete_gesture(std::uint32_t gesture_id) -> bool;
    [[nodiscard]] auto delete_gesture(const std::string& gesture_name) -> bool;
    [[nodiscard]] auto path() const -> const std::string&;

  private:
    void create_empty_schema() const;
    void backup_incompatible_schema() const;

    std::string path_;
  };

} // namespace signlang::signlang_det

#endif

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
  };

  class PrototypeDatabase {
  public:
    PrototypeDatabase(std::string path, std::uint32_t embedding_dim);

    PrototypeDatabase(const PrototypeDatabase&) = delete;
    auto operator=(const PrototypeDatabase&) -> PrototypeDatabase& = delete;
    PrototypeDatabase(PrototypeDatabase&&) = default;
    auto operator=(PrototypeDatabase&&) -> PrototypeDatabase& = default;

    void ensure_valid_empty_or_existing();

    [[nodiscard]] auto list_gestures() const -> std::vector<GestureInfo>;
    [[nodiscard]] auto load_gesture_samples(const std::string& gesture_name) const -> std::vector<EncodedSequence>;
    [[nodiscard]] auto replace_gesture_samples(const std::string& gesture_name,
                                               const std::vector<EncodedSequence>& samples) -> std::uint32_t;
    [[nodiscard]] auto delete_gesture(std::uint32_t gesture_id) -> bool;
    [[nodiscard]] auto delete_gesture(const std::string& gesture_name) -> bool;

    [[nodiscard]] auto path() const -> const std::string&;
    [[nodiscard]] auto embedding_dim() const -> std::uint32_t;

  private:
    [[nodiscard]] auto is_valid_schema() const -> bool;
    void recreate_empty_schema() const;

    std::string path_;
    std::uint32_t embedding_dim_;
  };

} // namespace signlang::signlang_det

#endif // SIGNLANG_EYES_SIGNLANG_DET_PROTOTYPE_DATABASE_HPP

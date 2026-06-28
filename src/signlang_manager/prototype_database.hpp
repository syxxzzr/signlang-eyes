#ifndef SIGNLANG_EYES_SIGNLANG_MANAGER_PROTOTYPE_DATABASE_HPP
#define SIGNLANG_EYES_SIGNLANG_MANAGER_PROTOTYPE_DATABASE_HPP

#include "gesture_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace signlang::signlang_manager {

  class PrototypeDatabase {
  public:
    PrototypeDatabase(std::string path, std::uint32_t embedding_dim);

    PrototypeDatabase(const PrototypeDatabase&) = delete;
    auto operator=(const PrototypeDatabase&) -> PrototypeDatabase& = delete;
    PrototypeDatabase(PrototypeDatabase&&) = default;
    auto operator=(PrototypeDatabase&&) -> PrototypeDatabase& = default;

    void ensure_valid_empty_or_existing();

    auto list_gestures() const -> std::vector<GestureInfo>;
    auto load_gesture_samples(const std::string& gesture_name) const -> std::vector<EncodedSequence>;
    auto add_gesture_sample(const std::string& gesture_name, const EncodedSequence& encoded_sample,
                            bool replace_existing) -> std::uint32_t;
    auto replace_gesture_samples(const std::string& gesture_name, const std::vector<EncodedSequence>& samples)
        -> std::uint32_t;
    auto delete_gesture(std::uint32_t gesture_id) -> bool;
    auto delete_gesture(const std::string& gesture_name) -> bool;

    auto path() const -> const std::string&;
    auto embedding_dim() const -> std::uint32_t;

  private:
    auto is_valid_schema() const -> bool;
    void recreate_empty_schema() const;

    std::string path_;
    std::uint32_t embedding_dim_;
  };

} // namespace signlang::signlang_manager

#endif // SIGNLANG_EYES_SIGNLANG_MANAGER_PROTOTYPE_DATABASE_HPP

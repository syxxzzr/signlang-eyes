#ifndef SIGNLANG_EYES_SIGNLANG_DET_GESTURE_PIPELINE_HPP
#define SIGNLANG_EYES_SIGNLANG_DET_GESTURE_PIPELINE_HPP

#include "signlang_result.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace signlang::signlang_det {

  constexpr auto kMaxSequenceLength = std::uint32_t{64};
  constexpr auto kMaxCapturedFrames = std::uint32_t{120};
  constexpr auto kEmbeddingDim = std::uint32_t{128};
  constexpr auto kPaddingValue = -100.0F;

  struct SegmentQuality {
    float present_frame_ratio{0.0F};
    float mean_confidence{0.0F};
    float peak_motion{0.0F};
    float score{0.0F};
  };

  struct GestureSegment {
    std::vector<FeatureVector> frames;
    SegmentQuality quality;
    bool forced_max_length{false};
  };

  struct RecordedHandposeFrame {
    handpose_det::HandPoseFrameMetadata metadata;
    std::array<handpose_det::HandPoseDetection, kMaxHandCount> detections;
    std::uint32_t detection_count;
  };

  struct PackedSegment {
    std::array<float, kMaxSequenceLength * kFeatureDim> landmarks;
    std::uint32_t valid_length{0};
    std::uint32_t original_length{0};
    std::uint64_t start_sequence{0};
    std::uint64_t end_sequence{0};
    std::uint64_t start_timestamp_ns{0};
    std::uint64_t end_timestamp_ns{0};
    SegmentQuality quality;
    bool forced_max_length{false};
  };

  using FrameEmbedding = std::array<float, kEmbeddingDim>;

  struct EncodedGesture {
    std::vector<FrameEmbedding> frames;
    FrameEmbedding pooled{};
    PackedSegment segment;
  };

  struct SegmenterOptions {
    std::uint32_t pre_roll_frames{6};
    std::uint32_t start_confirmation_frames{3};
    std::uint32_t end_confirmation_frames{6};
    std::uint32_t post_roll_frames{3};
    std::uint32_t minimum_frames{12};
    std::uint32_t maximum_frames{kMaxCapturedFrames};
    std::uint32_t cooldown_quiet_frames{6};
    std::uint32_t cooldown_ms{300};
    float start_motion_threshold{0.08F};
    float end_motion_threshold{0.03F};
    float static_presence_threshold{0.6F};
    float motion_ema_alpha{0.5F};
  };

  enum class SegmentEvent { None, Emitted, DroppedTooShort, DroppedSequenceGap };

  struct SegmentUpdate {
    SegmentEvent event{SegmentEvent::None};
    std::optional<GestureSegment> segment;
  };

  class GestureSegmenter {
  public:
    explicit GestureSegmenter(SegmenterOptions options);
    [[nodiscard]] auto push(const FeatureVector& frame) -> SegmentUpdate;
    void reset();

  private:
    enum class State { Idle, Arming, Active, Ending, Cooldown };
    [[nodiscard]] static auto has_hand(const FeatureVector& frame) -> bool;
    [[nodiscard]] auto close_segment(bool forced) -> SegmentUpdate;
    void remember_pre_roll(const FeatureVector& frame);
    void enter_idle();

    SegmenterOptions options_;
    State state_{State::Idle};
    std::deque<FeatureVector> pre_roll_;
    std::vector<FeatureVector> active_frames_;
    std::uint32_t confirmation_count_{0};
    std::uint32_t quiet_count_{0};
    std::uint64_t cooldown_started_ns_{0};
    float smoothed_motion_{0.0F};
    bool previous_present_{false};
    bool arming_static_{false};
    bool static_action_{false};
  };

  struct PreprocessingOptions {
    std::uint32_t minimum_frames{12};
    float minimum_present_frame_ratio{0.5F};
    float minimum_mean_confidence{0.3F};
    float minimum_quality{0.15F};
  };

  class SegmentPreprocessor {
  public:
    explicit SegmentPreprocessor(PreprocessingOptions options);
    [[nodiscard]] auto evaluate(std::vector<FeatureVector> frames, bool forced) const -> GestureSegment;
    [[nodiscard]] auto prepare(const GestureSegment& segment) const -> PackedSegment;

  private:
    [[nodiscard]] static auto has_hand(const FeatureVector& frame) -> bool;
    [[nodiscard]] static auto resample(const std::vector<FeatureVector>& frames) -> std::vector<FeatureVector>;
    static void write_frame(const FeatureVector& frame, float* destination);
    PreprocessingOptions options_;
  };

} // namespace signlang::signlang_det

#endif

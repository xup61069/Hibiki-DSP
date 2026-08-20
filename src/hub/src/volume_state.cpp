#include "hibiki/volume_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace hibiki {

double effective_gain_db(const double requested_db, const double safety_ceiling_db) noexcept {
    if (!std::isfinite(requested_db) || !std::isfinite(safety_ceiling_db)) {
        return -144.0;
    }
    return std::clamp(std::min(requested_db, safety_ceiling_db), -144.0, 12.0);
}

std::int32_t db_to_q16_16(const double db) noexcept {
    if (!std::isfinite(db)) {
        return -144 * 65536;
    }
    const double limited = std::clamp(db, -144.0, 12.0);
    return static_cast<std::int32_t>(std::llround(limited * 65536.0));
}

double q16_16_to_db(const std::int32_t value) noexcept {
    return std::clamp(static_cast<double>(value) / 65536.0, -144.0, 12.0);
}

OutputGroupVolumeStateV1 reconcile(OutputGroupVolumeStateV1 state) noexcept {
    state.schema_version = 1;
    state.effective_db = effective_gain_db(state.requested_db, state.safety_ceiling_db);
    if (state.actuator == ActuatorMode::StrictDirect) {
        state.effective_db = 0.0;
    }
    return state;
}

VolumeRamp make_ramp(const OutputGroupVolumeStateV1& before,
                     const OutputGroupVolumeStateV1& after) noexcept {
    const bool mute_transition = before.mute != after.mute;
    const std::uint32_t duration = mute_transition ? (after.mute ? 5U : 15U) : 8U;
    return VolumeRamp{before.effective_db, after.effective_db, duration};
}

void VolumeRampProcessorV1::reset(const double current_db, const bool mute) noexcept {
    current_db_ = std::isfinite(current_db) ? std::clamp(current_db, -144.0, 12.0) : -60.0;
    target_db_ = mute ? -144.0 : current_db_;
    target_q16_16_ = db_to_q16_16(current_db_);
    remaining_frames_ = 0U;
    target_mute_ = mute;
    initialized_ = true;
}

void VolumeRampProcessorV1::observe_target(const std::int32_t effective_db_q16_16,
                                           const bool mute,
                                           std::uint32_t sample_rate) noexcept {
    if (sample_rate < 8000U || sample_rate > 192000U) sample_rate = 48000U;
    if (initialized_ && target_q16_16_ == effective_db_q16_16 && target_mute_ == mute) {
        return;
    }

    if (!initialized_) {
        reset(-60.0, false);
    }
    const auto previous_mute = target_mute_;
    const auto target_db = mute ? -144.0 : q16_16_to_db(effective_db_q16_16);
    const auto duration_ms = mute != previous_mute ? (mute ? 5U : 15U) : 8U;
    const auto frame_count = static_cast<std::uint64_t>(sample_rate) * duration_ms / 1000U;
    target_db_ = std::isfinite(target_db) ? std::clamp(target_db, -144.0, 12.0) : -144.0;
    target_q16_16_ = effective_db_q16_16;
    target_mute_ = mute;
    remaining_frames_ = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
        frame_count, 1U, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
}

float VolumeRampProcessorV1::next_gain() noexcept {
    if (remaining_frames_ > 0U) {
        current_db_ += (target_db_ - current_db_) / static_cast<double>(remaining_frames_);
        --remaining_frames_;
        if (remaining_frames_ == 0U) current_db_ = target_db_;
    }
    if (target_mute_ && remaining_frames_ == 0U) return 0.0F;
    if (!std::isfinite(current_db_) || current_db_ <= -144.0) return 0.0F;
    return static_cast<float>(std::pow(10.0, current_db_ / 20.0));
}

VolumeNotificationResult apply_windows_notification(
    OutputGroupVolumeStateV1& state, const VolumeNotificationV1& notification) noexcept {
    if (!std::isfinite(notification.requested_db) ||
        notification.requested_db < -144.0 || notification.requested_db > 12.0) {
        return VolumeNotificationResult::Invalid;
    }
    if (notification.generation < state.generation) {
        return VolumeNotificationResult::StaleGeneration;
    }
    state.requested_db = notification.requested_db;
    state.mute = notification.mute;
    state.generation = notification.generation;
    state.origin = VolumeOrigin::Windows;
    state = reconcile(state);
    return VolumeNotificationResult::Accepted;
}

}  // namespace hibiki

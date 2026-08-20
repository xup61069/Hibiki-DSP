#include "hibiki/volume_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

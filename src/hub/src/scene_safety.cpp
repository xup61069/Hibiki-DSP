// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_safety.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

constexpr double kExternalChangeToleranceDb = 0.25;

bool approximately_equal(const double left, const double right) noexcept {
    return std::isfinite(left) && std::isfinite(right) &&
           std::abs(left - right) <= kExternalChangeToleranceDb;
}

double safe_baseline(const OutputGroupVolumeStateV1& current) noexcept {
    if (!std::isfinite(current.requested_db) || !std::isfinite(current.safety_ceiling_db)) {
        return -60.0;
    }
    return std::min(current.requested_db, current.safety_ceiling_db);
}

}  // namespace

bool SceneSafetyController::begin(const SceneProfileV1& scene,
                                   const OutputGroupVolumeStateV1& current) noexcept {
    if (scene.schema_version != 1 || !std::isfinite(scene.limiter_dbtp) ||
        scene.limiter_dbtp > 0.0 || scene.limiter_dbtp < -24.0 ||
        !std::isfinite(current.requested_db) || !std::isfinite(current.safety_ceiling_db)) {
        return false;
    }
    active_ = true;
    auto_attenuate_ = scene.auto_attenuate;
    user_override_ = false;
    baseline_db_ = safe_baseline(current);
    last_controller_db_ = baseline_db_;
    limiter_dbtp_ = scene.limiter_dbtp;
    last_action_ms_ = 0;
    return true;
}

SceneSafetyActionV1 SceneSafetyController::observe_peak(
    const double peak_dbtp,
    const std::uint64_t now_ms,
    const OutputGroupVolumeStateV1& current) noexcept {
    SceneSafetyActionV1 action{};
    if (!active_ || !auto_attenuate_ || user_override_ || !std::isfinite(peak_dbtp) ||
        !std::isfinite(current.requested_db) ||
        !approximately_equal(current.requested_db, last_controller_db_)) {
        if (active_ && !approximately_equal(current.requested_db, last_controller_db_)) {
            user_override_ = true;
        }
        return action;
    }
    if (now_ms < last_action_ms_ || now_ms - last_action_ms_ < kMinimumActionIntervalMs ||
        peak_dbtp <= limiter_dbtp_ + kPeakHysteresisDb) {
        return action;
    }

    const auto overage_db = std::min(kMaxAttenuationStepDb, peak_dbtp - limiter_dbtp_);
    const auto target_db = std::max(-144.0, current.requested_db - overage_db);
    if (target_db >= current.requested_db - kPeakHysteresisDb) {
        return action;
    }
    last_controller_db_ = target_db;
    last_action_ms_ = now_ms;
    action.kind = SceneSafetyActionKind::Attenuate;
    action.requested_db = target_db;
    action.origin = VolumeOrigin::Safety;
    return action;
}

SceneSafetyActionV1 SceneSafetyController::end(
    const OutputGroupVolumeStateV1& current) noexcept {
    SceneSafetyActionV1 action{};
    if (!active_) return action;

    if (!approximately_equal(current.requested_db, last_controller_db_)) {
        user_override_ = true;
    }

    if (auto_attenuate_ && !user_override_ &&
        approximately_equal(current.requested_db, last_controller_db_)) {
        const auto target_db = std::min(baseline_db_, current.safety_ceiling_db);
        if (target_db > current.requested_db + kPeakHysteresisDb) {
            action.kind = SceneSafetyActionKind::Restore;
            action.requested_db = target_db;
            action.origin = VolumeOrigin::Scene;
        }
    }
    active_ = false;
    return action;
}

}  // namespace hibiki

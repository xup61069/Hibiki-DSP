#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/contracts.hpp"
#include "hibiki/volume_state.hpp"

#include <cstdint>

namespace hibiki {

enum class SceneSafetyActionKind : std::uint8_t {
    None,
    Attenuate,
    Restore,
};

struct SceneSafetyActionV1 {
    SceneSafetyActionKind kind{SceneSafetyActionKind::None};
    double requested_db{-60.0};
    VolumeOrigin origin{VolumeOrigin::Safety};
};

// Control-plane-only policy for scenes that may be louder than the user's
// remembered baseline. It never writes Windows directly: callers submit the
// returned dB target through VolumeBroker with a Safety/Scene GUID.
class SceneSafetyController final {
public:
    [[nodiscard]] bool begin(const SceneProfileV1& scene,
                             const OutputGroupVolumeStateV1& current) noexcept;

    [[nodiscard]] SceneSafetyActionV1 observe_peak(
        double peak_dbtp,
        std::uint64_t now_ms,
        const OutputGroupVolumeStateV1& current) noexcept;

    [[nodiscard]] SceneSafetyActionV1 end(
        const OutputGroupVolumeStateV1& current) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool user_override_detected() const noexcept { return user_override_; }
    [[nodiscard]] double baseline_db() const noexcept { return baseline_db_; }

private:
    static constexpr double kPeakHysteresisDb = 0.5;
    static constexpr double kMaxAttenuationStepDb = 3.0;
    static constexpr std::uint64_t kMinimumActionIntervalMs = 100;

    bool active_{false};
    bool auto_attenuate_{false};
    bool user_override_{false};
    double baseline_db_{-60.0};
    double last_controller_db_{-60.0};
    double limiter_dbtp_{-1.0};
    std::uint64_t last_action_ms_{0};
};

}  // namespace hibiki

#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>

namespace hibiki {

enum class VolumeOrigin : std::uint8_t {
    Windows,
    HibikiUi,
    Safety,
    Scene,
    Session,
};

enum class ActuatorMode : std::uint8_t {
    InternalDsp,
    DeviceHardware,
    StrictDirect,
};

struct OutputGroupVolumeStateV1 {
    std::uint32_t schema_version{1};
    double requested_db{-60.0};
    double safety_ceiling_db{0.0};
    double effective_db{-60.0};
    bool mute{false};
    std::uint64_t generation{0};
    VolumeOrigin origin{VolumeOrigin::Windows};
    ActuatorMode actuator{ActuatorMode::InternalDsp};
};

struct VolumeRamp {
    double start_db{0.0};
    double target_db{0.0};
    std::uint32_t duration_ms{8};
};

// Control-plane mirror of a driver/IAudioEndpointVolume notification. The
// Windows callback copies these fields into a queue; the RT thread never calls
// COM or allocates. Generation is the canonical ordering key.
struct VolumeNotificationV1 {
    double requested_db{-60.0};
    bool mute{false};
    std::uint64_t generation{0};
};

enum class VolumeNotificationResult : std::uint8_t {
    Accepted,
    StaleGeneration,
    Invalid,
};

[[nodiscard]] double effective_gain_db(double requested_db, double safety_ceiling_db) noexcept;
[[nodiscard]] std::int32_t db_to_q16_16(double db) noexcept;
[[nodiscard]] double q16_16_to_db(std::int32_t value) noexcept;
[[nodiscard]] OutputGroupVolumeStateV1 reconcile(OutputGroupVolumeStateV1 state) noexcept;
[[nodiscard]] VolumeRamp make_ramp(const OutputGroupVolumeStateV1& before,
                                    const OutputGroupVolumeStateV1& after) noexcept;
[[nodiscard]] VolumeNotificationResult apply_windows_notification(
    OutputGroupVolumeStateV1& state, const VolumeNotificationV1& notification) noexcept;

}  // namespace hibiki

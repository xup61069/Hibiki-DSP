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

// RT-owned, allocation-free ramp state. Targets are observed from the
// control-plane Q16.16 snapshot; the state itself is advanced only by the
// audio thread in dB-domain, with mute transitions using their dedicated
// durations.
class VolumeRampProcessorV1 final {
public:
    void observe_target(std::int32_t effective_db_q16_16,
                        bool mute,
                        std::uint32_t sample_rate) noexcept;
    [[nodiscard]] float next_gain() noexcept;
    void reset(double current_db = -60.0, bool mute = false) noexcept;
    [[nodiscard]] double current_db() const noexcept { return current_db_; }
    [[nodiscard]] std::uint32_t remaining_frames() const noexcept { return remaining_frames_; }

private:
    double current_db_{-60.0};
    double target_db_{-60.0};
    std::int32_t target_q16_16_{-60 * 65536};
    std::uint32_t remaining_frames_{0U};
    bool target_mute_{false};
    bool initialized_{false};
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

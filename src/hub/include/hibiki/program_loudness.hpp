#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <array>

namespace hibiki {

// Slow content-aware level correction. The default remains a bounded RMS proxy
// for backwards compatibility. KWeightedProxy adds the two fixed K-weighting
// sections used by the ITU-R BS.1770 family, but this class is still not a
// conformance meter: it has no gated loudness blocks, true-peak oracle, or
// channel-layout metadata. It must not be presented as formal BS.1770 or ISO
// 226 conformance.
enum class ProgramAwareMeterModeV1 : std::uint8_t {
    RmsProxy = 0,
    KWeightedProxy = 1,
};

struct ProgramAwareLevelPolicyV1 {
    std::uint32_t schema_version{1};
    bool enabled{false};
    double target_dbfs{-23.0};
    double max_boost_db{6.0};
    double max_cut_db{12.0};
    double analysis_window_ms{3000.0};
    double max_rate_db_per_second{6.0};
    double silence_gate_dbfs{-70.0};
    ProgramAwareMeterModeV1 meter_mode{ProgramAwareMeterModeV1::RmsProxy};
    // Optional stream channel index to exclude from program loudness. A value
    // below zero keeps every channel; callers may set 3 for the usual LPCM
    // L/R/C/LFE/… order. This is a hint, not a channel-layout assertion.
    std::int32_t excluded_channel{-1};
};

struct ProgramAwareLevelStatusV1 {
    std::uint32_t schema_version{1};
    bool valid{false};
    bool enabled{false};
    bool silence_gated{true};
    double measured_dbfs{-144.0};
    double desired_gain_db{0.0};
    double applied_gain_db{0.0};
    ProgramAwareMeterModeV1 meter_mode{ProgramAwareMeterModeV1::RmsProxy};
};

[[nodiscard]] bool validate_program_aware_policy(
    const ProgramAwareLevelPolicyV1& policy) noexcept;

// RT-owned, allocation-free level controller. The caller supplies the audio
// block; process only copies scalar state and never allocates or waits.
class ProgramAwareLevelControllerV1 final {
public:
    [[nodiscard]] bool configure(const ProgramAwareLevelPolicyV1& policy,
                                 std::uint32_t sample_rate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process_interleaved(float* interleaved,
                                           std::size_t frames,
                                           std::uint32_t channels) noexcept;
    [[nodiscard]] const ProgramAwareLevelStatusV1& status() const noexcept {
        return status_;
    }
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }

public:
    // Exposed only so the control-side coefficient builder can remain a
    // small, allocation-free translation unit helper; callers should treat
    // these as implementation details.
    struct Biquad {
        double b0{1.0};
        double b1{0.0};
        double b2{0.0};
        double a1{0.0};
        double a2{0.0};
    };

    struct BiquadState {
        double x1{0.0};
        double x2{0.0};
        double y1{0.0};
        double y2{0.0};
    };

private:
    ProgramAwareLevelPolicyV1 policy_{};
    ProgramAwareLevelStatusV1 status_{};
    std::uint32_t sample_rate_{0U};
    bool configured_{false};
    double smoothed_energy_{0.0};

    std::array<Biquad, 2U> k_weighting_{};
    std::array<std::array<BiquadState, 8U>, 2U> k_state_{};
};

}  // namespace hibiki

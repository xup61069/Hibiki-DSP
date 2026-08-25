#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <string_view>

namespace hibiki {

inline constexpr std::size_t kTelemetryDoubleCountV1 = 3U;

// Slow content-aware level correction. The default remains a bounded RMS proxy
// for backwards compatibility. KWeightedProxy adds the two fixed K-weighting
// sections used by the ITU-R BS.1770 family, but this class is still not a
// conformance meter: it has no gated loudness blocks, true-peak oracle, or
// channel-layout metadata. It must not be presented as formal BS.1770 or equal-loudness
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
    // Opt-in content-driven bass correction. When enabled, a fixed one-pole
    // low-pass (~120 Hz) measures the bass-to-broadband energy ratio over a
    // sliding two-second window and applies a bounded low-shelf attenuation
    // on the RT thread when bass clearly dominates.
    bool bass_correction_enabled{false};
    double bass_max_cut_db{6.0};
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
    double bass_correction_gain_db{0.0};
};

// Control-plane projection of RT-owned status. This is bounded visual
// telemetry only; it is not a BS.1770 meter, equal-loudness curve, or physical-audio
// evidence.
struct ProgramAwareTelemetrySnapshotV1 {
    bool valid{false};
    bool enabled{false};
    bool silence_gated{true};
    double measured_dbfs{-144.0};
    double applied_gain_db{0.0};
    double bass_correction_gain_db{0.0};
    std::uint64_t sequence{0U};
};

[[nodiscard]] bool validate_program_aware_policy(
    const ProgramAwareLevelPolicyV1& policy) noexcept;

// RT-safe bass-excess detector. One-pole low-pass per channel (~120 Hz),
// exponentially smoothed bass/broadband energy ratio over a ~2 s window.
// No allocation, lock, wait, or platform call.
class BassExcessDetectorV1 final {
public:
    BassExcessDetectorV1() noexcept = default;
    BassExcessDetectorV1(const BassExcessDetectorV1&) = delete;
    BassExcessDetectorV1& operator=(const BassExcessDetectorV1&) = delete;
    [[nodiscard]] bool configure(std::uint32_t sample_rate) noexcept;
    void reset() noexcept;
    // Processes one interleaved block read-only and updates the smoothed
    // bass-excess estimate. Returns false on invalid input.
    [[nodiscard]] bool process(const float* interleaved, std::size_t frames,
                               std::uint32_t channels) noexcept;
    // Smoothed dB difference 20*log10(band_rms / total_rms); <= 0. Near 0
    // means bass-dominated content. Returns -144 before enough data.
    [[nodiscard]] double smoothed_excess_db() const noexcept { return smoothed_excess_db_; }

private:
    static constexpr double kBassCutoffHz = 120.0;
    static constexpr double kWindowSeconds = 2.0;
    std::uint32_t sample_rate_{0U};
    std::array<double, 8U> lp_state_{};
    double smoothed_band_energy_{0.0};
    double smoothed_total_energy_{0.0};
    double smoothed_excess_db_{-144.0};
    bool window_started_{false};
};

// RT-owned, allocation-free level controller. The caller supplies the audio
// block; process only copies scalar state and never allocates or waits.
class ProgramAwareLevelControllerV1 final {
public:
    ProgramAwareLevelControllerV1() noexcept = default;
    ProgramAwareLevelControllerV1(const ProgramAwareLevelControllerV1&) = delete;
    ProgramAwareLevelControllerV1& operator=(
        const ProgramAwareLevelControllerV1&) = delete;
    [[nodiscard]] bool configure(const ProgramAwareLevelPolicyV1& policy,
                                 std::uint32_t sample_rate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process_interleaved(float* interleaved,
                                           std::size_t frames,
                                           std::uint32_t channels) noexcept;
    [[nodiscard]] const ProgramAwareLevelStatusV1& status() const noexcept {
        return status_;
    }
    // Lock-free projection for the control worker after the audio callback's
    // current block has completed. Reads are not synchronized with an in-
    // flight render; every field is independently finite and bounded.
    [[nodiscard]] ProgramAwareTelemetrySnapshotV1 read_telemetry() const noexcept;
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

    void store_telemetry() noexcept;

private:
    ProgramAwareLevelPolicyV1 policy_{};
    ProgramAwareLevelStatusV1 status_{};
    std::uint32_t sample_rate_{0U};
    bool configured_{false};
    double smoothed_energy_{0.0};
    double bass_cut_db_{0.0};
    BassExcessDetectorV1 bass_detector_{};

    std::array<Biquad, 2U> k_weighting_{};
    std::array<std::array<BiquadState, 8U>, 2U> k_state_{};

    mutable std::array<std::atomic<double>, kTelemetryDoubleCountV1>
        telemetry_doubles_{};
    mutable std::atomic<bool> telemetry_valid_{false};
    mutable std::atomic<bool> telemetry_enabled_{false};
    mutable std::atomic<bool> telemetry_silence_gated_{true};
    mutable std::atomic<std::uint64_t> telemetry_sequence_{0U};
};

// Fixed-capacity per-output-group program-aware level attachment. The
// control plane registers and configures slots before commit; the RT thread
// looks up a slot by group label without allocation, lock or platform call.
// This mirrors the OutputGroupVolumeBankV1 ownership pattern.
constexpr std::size_t kMaxProgramAwareGroupsV1 = 32U;
constexpr std::size_t kMaxProgramAwareGroupBytesV1 = 64U;

class ProgramAwareLevelBankV1 final {
public:
    ProgramAwareLevelBankV1() noexcept = default;
    ProgramAwareLevelBankV1(const ProgramAwareLevelBankV1&) = delete;
    ProgramAwareLevelBankV1& operator=(const ProgramAwareLevelBankV1&) = delete;

    // Control-plane operation. Registration never allocates and is
    // idempotent for an existing label. Fails closed at capacity.
    [[nodiscard]] bool register_group(std::string_view output_group) noexcept;
    // RT-only lookup. Returns nullptr when the group is not registered.
    [[nodiscard]] ProgramAwareLevelControllerV1* controller_for_group(
        std::string_view output_group) const noexcept;
    // Control-plane operation. Configures (or replaces) the policy of one
    // registered group; the RT state is reset so no stale gain survives.
    [[nodiscard]] bool configure_group(
        std::string_view output_group,
        const ProgramAwareLevelPolicyV1& policy,
        std::uint32_t sample_rate) noexcept;
    // Resets every configured controller to unity gain and clears policies.
    void reset_all() noexcept;
    [[nodiscard]] bool has_group(std::string_view output_group) const noexcept;
    [[nodiscard]] std::size_t group_count() const noexcept { return group_count_; }

private:
    struct Slot {
        bool used{false};
        std::array<char, kMaxProgramAwareGroupBytesV1> group{};
        ProgramAwareLevelPolicyV1 policy{};
        mutable ProgramAwareLevelControllerV1 controller{};
    };

    [[nodiscard]] Slot* find_slot(std::string_view output_group) noexcept;
    [[nodiscard]] const Slot* find_slot(std::string_view output_group) const noexcept;
    static bool valid_group(std::string_view output_group) noexcept;

    std::array<Slot, kMaxProgramAwareGroupsV1> slots_{};
    std::size_t group_count_{0U};
};

}  // namespace hibiki

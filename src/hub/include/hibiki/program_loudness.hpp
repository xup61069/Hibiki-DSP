#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>

namespace hibiki {

// Slow content-aware level correction. The measurement is a bounded RMS proxy
// for the future BS.1770/K-weighted analyzer; it must not be presented as a
// BS.1770 conformance result or as an ISO 226 contour.
struct ProgramAwareLevelPolicyV1 {
    std::uint32_t schema_version{1};
    bool enabled{false};
    double target_dbfs{-23.0};
    double max_boost_db{6.0};
    double max_cut_db{12.0};
    double analysis_window_ms{3000.0};
    double max_rate_db_per_second{6.0};
    double silence_gate_dbfs{-70.0};
};

struct ProgramAwareLevelStatusV1 {
    std::uint32_t schema_version{1};
    bool valid{false};
    bool enabled{false};
    bool silence_gated{true};
    double measured_dbfs{-144.0};
    double desired_gain_db{0.0};
    double applied_gain_db{0.0};
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

private:
    ProgramAwareLevelPolicyV1 policy_{};
    ProgramAwareLevelStatusV1 status_{};
    std::uint32_t sample_rate_{0U};
    bool configured_{false};
    double smoothed_energy_{0.0};
};

}  // namespace hibiki

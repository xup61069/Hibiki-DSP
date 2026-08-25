#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/exporters.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hibiki {

// A control-plane frequency-response sample.  measured_db and target_db are
// caller-supplied values (for example from REW or a legal calibration file);
// no microphone data or equal-loudness table is embedded here.
struct CalibrationResponsePointV1 {
    double frequency_hz{0.0};
    double measured_db{0.0};
    double target_db{0.0};
};

struct CalibrationCompilePolicyV1 {
    std::uint32_t schema_version{1};
    std::uint32_t max_filters{16};
    double min_frequency_hz{20.0};
    double max_frequency_hz{20000.0};
    double max_boost_db{6.0};
    double max_cut_db{12.0};
    double min_q{0.3};
    double max_q{12.0};
    double min_spacing_octaves{1.0 / 12.0};
    double ignore_error_db{0.25};
};

struct CalibrationCompileResultV1 {
    std::vector<PeqFilterV1> filters;
    bool limited{false};
    double maximum_requested_correction_db{0.0};
    std::string diagnostic;
};

[[nodiscard]] bool validate_calibration_compile_policy_v1(
    const CalibrationCompilePolicyV1& policy) noexcept;

[[nodiscard]] bool validate_calibration_response_v1(
    std::span<const CalibrationResponsePointV1> response,
    const CalibrationCompilePolicyV1& policy) noexcept;

// Deterministic, bounded control-plane compiler. It greedily retains the
// largest log-frequency-spaced residuals and emits RBJ-compatible peaking
// filters. It is intentionally not a room optimizer: the result must be
// measured again before being used as a safety-critical profile.
[[nodiscard]] CalibrationCompileResultV1 compile_bounded_peq_correction_v1(
    std::span<const CalibrationResponsePointV1> response,
    const CalibrationCompilePolicyV1& policy = {});

}  // namespace hibiki

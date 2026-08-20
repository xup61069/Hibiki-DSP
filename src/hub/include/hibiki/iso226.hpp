#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hibiki {

struct IsoContourPoint {
    double frequency_hz{0.0};
    double spl_db{0.0};
};

enum class EqualLoudnessMode : std::uint8_t {
    Calibrated,
    Relative,
    ProgramAware,
};

struct EqualLoudnessPolicyV1 {
    std::uint32_t schema_version{1};
    std::string standard{"iso-226-2023-derived"};
    EqualLoudnessMode mode{EqualLoudnessMode::Relative};
    double reference_phon{80.0};
    double strength{1.0};
    double max_boost_db{6.0};
    double measured_f3_hz{0.0};
    std::string anchor_id;
    // Kept as a source-compatible convenience for early callers. New code
    // should use mode == EqualLoudnessMode::Calibrated.
    bool calibrated{false};
};

struct CompensationPoint {
    double frequency_hz{0.0};
    double gain_db{0.0};
    bool limited{false};
};

struct CompensationResult {
    std::vector<CompensationPoint> points;
    bool limited{false};
    std::string diagnostic;
};

struct EqualLoudnessStatusV1 {
    std::uint32_t schema_version{1};
    EqualLoudnessMode mode{EqualLoudnessMode::Relative};
    bool calibrated{false};
    bool limited{false};
    double maximum_fit_error_db{0.0};
    double realized_peak_db{0.0};
    std::string diagnostic;
};

[[nodiscard]] bool validate_policy(const EqualLoudnessPolicyV1& policy) noexcept;

// The caller supplies legally obtained ISO contour values. This module does
// not embed the licensed ISO document or a restricted Annex table.
[[nodiscard]] CompensationResult build_compensation(
    const std::vector<IsoContourPoint>& current,
    const std::vector<IsoContourPoint>& reference,
    const EqualLoudnessPolicyV1& policy) noexcept;

}  // namespace hibiki

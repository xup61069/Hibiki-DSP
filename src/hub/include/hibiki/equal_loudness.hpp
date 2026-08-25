#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hibiki {

struct EqualLoudnessContourPoint {
    double frequency_hz{0.0};
    double spl_db{0.0};
};

struct EqualLoudnessFormulaPointV1 {
    double frequency_hz{1000.0};
    double alpha_f{0.30};
    double threshold_db{2.4};
    double transfer_db{0.0};
};

struct EqualLoudnessFormulaReferenceV1 {
    double reference_alpha{0.30};
    double reference_threshold_db{2.4};
};

enum class EqualLoudnessMode : std::uint8_t {
    Calibrated,
    Relative,
    ProgramAware,
};

struct EqualLoudnessPolicyV1 {
    std::uint32_t schema_version{1};
    std::string standard{"equal-loudness-derived"};
    EqualLoudnessMode mode{EqualLoudnessMode::Relative};
    double reference_phon{80.0};
    double strength{1.0};
    double max_boost_db{6.0};
    double measured_f3_hz{0.0};
    std::string anchor_id;
    // Opt-in live phon recompute for the scene's equal-loudness attachment.
    // Disabled is the safe default; every explicit attachment prepare resets
    // to disabled (existing behavior, unchanged).
    bool live_update_enabled{false};
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
// equal-loudness formula with caller-supplied, legally obtained parameters. The
// normative phon domain is frequency dependent: 20..90 through 4 kHz and
// 20..80 from just above 4 kHz through 12.5 kHz. Informative 0/10-phon values
// are rejected. No standard coefficient table is embedded in this repository.
[[nodiscard]] bool equal_loudness_spl_from_phon(const EqualLoudnessFormulaPointV1& point,
                                        const EqualLoudnessFormulaReferenceV1& reference,
                                        double phon,
                                        double& spl_db) noexcept;

// The caller supplies legally obtained equal-loudness contour values. This module does
// not embed the licensed equal-loudness document or a restricted Annex table.
[[nodiscard]] CompensationResult build_compensation(
    const std::vector<EqualLoudnessContourPoint>& current,
    const std::vector<EqualLoudnessContourPoint>& reference,
    const EqualLoudnessPolicyV1& policy) noexcept;

// Compute the normalized equal-loudness compensation directly from caller-supplied
// equal-loudness formula points. The point table is intentionally not embedded here:
// the caller must provide legally obtained standard data. Each
// current/reference evaluation is restricted to the frequency-dependent
// normative phon domain; any out-of-range point fails the complete result.
[[nodiscard]] CompensationResult build_formula_compensation(
    std::span<const EqualLoudnessFormulaPointV1> points,
    double current_phon,
    const EqualLoudnessPolicyV1& policy) noexcept;

}  // namespace hibiki

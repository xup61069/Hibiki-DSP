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

struct Iso226FormulaPointV1 {
    double frequency_hz{1000.0};
    double alpha_f{0.30};
    double threshold_db{2.4};
    double transfer_db{0.0};
};

struct Iso226FormulaReferenceV1 {
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
// ISO 226:2023 formula with caller-supplied, legally obtained parameters. No
// standard coefficient table is embedded in this repository.
[[nodiscard]] bool iso226_spl_from_phon(const Iso226FormulaPointV1& point,
                                        const Iso226FormulaReferenceV1& reference,
                                        double phon,
                                        double& spl_db) noexcept;

// The caller supplies legally obtained ISO contour values. This module does
// not embed the licensed ISO document or a restricted Annex table.
[[nodiscard]] CompensationResult build_compensation(
    const std::vector<IsoContourPoint>& current,
    const std::vector<IsoContourPoint>& reference,
    const EqualLoudnessPolicyV1& policy) noexcept;

}  // namespace hibiki

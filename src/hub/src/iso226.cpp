#include "hibiki/iso226.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {
namespace {

constexpr double kOneKhz = 1000.0;
constexpr double kMinFrequency = 20.0;
constexpr double kMaxFrequency = 12500.0;

bool valid_mode(const EqualLoudnessMode mode) noexcept {
    return mode == EqualLoudnessMode::Calibrated || mode == EqualLoudnessMode::Relative ||
           mode == EqualLoudnessMode::ProgramAware;
}

double value_at_1khz(const std::vector<IsoContourPoint>& curve) noexcept {
    for (const auto& point : curve) {
        if (std::abs(point.frequency_hz - kOneKhz) < 1e-6) {
            return point.spl_db;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

bool validate_policy(const EqualLoudnessPolicyV1& policy) noexcept {
    if (policy.schema_version != 1 ||
        (policy.standard != "iso-226-2023-derived" &&
         policy.standard != "iso-226-2023-calibrated") ||
        !valid_mode(policy.mode) || !std::isfinite(policy.reference_phon) ||
        policy.reference_phon < 20.0 || policy.reference_phon > 90.0 ||
        !std::isfinite(policy.strength) || policy.strength < 0.0 || policy.strength > 1.0 ||
        !std::isfinite(policy.max_boost_db) || policy.max_boost_db < 0.0 ||
        policy.max_boost_db > 12.0 || !std::isfinite(policy.measured_f3_hz) ||
        policy.measured_f3_hz < 0.0) {
        return false;
    }
    if (policy.mode == EqualLoudnessMode::Calibrated && policy.anchor_id.empty()) {
        return false;
    }
    if (policy.mode == EqualLoudnessMode::Calibrated &&
        policy.standard != "iso-226-2023-calibrated") {
        return false;
    }
    if (policy.mode == EqualLoudnessMode::Relative && policy.calibrated) {
        return false;
    }
    return true;
}

CompensationResult build_compensation(const std::vector<IsoContourPoint>& current,
                                      const std::vector<IsoContourPoint>& reference,
                                      const EqualLoudnessPolicyV1& policy) noexcept {
    CompensationResult result;
    if (!validate_policy(policy)) {
        result.diagnostic = "invalid ISO policy; check mode, phon, strength, cap and anchor";
        return result;
    }
    if (current.empty() || current.size() != reference.size()) {
        result.diagnostic = "ISO contour arrays must be non-empty and have equal length";
        return result;
    }

    const double current_1k = value_at_1khz(current);
    const double reference_1k = value_at_1khz(reference);
    if (!std::isfinite(current_1k) || !std::isfinite(reference_1k)) {
        result.diagnostic = "ISO contour arrays must contain a 1 kHz anchor";
        return result;
    }

    result.points.reserve(current.size());
    for (std::size_t index = 0; index < current.size(); ++index) {
        const auto& c = current[index];
        const auto& r = reference[index];
        if (std::abs(c.frequency_hz - r.frequency_hz) > 1e-6 ||
            !std::isfinite(c.frequency_hz) || !std::isfinite(c.spl_db) || !std::isfinite(r.spl_db)) {
            result.points.clear();
            result.diagnostic = "ISO contour frequencies must align and all values must be finite";
            return result;
        }

        double gain = policy.strength * ((c.spl_db - current_1k) - (r.spl_db - reference_1k));
        bool limited = false;
        if (c.frequency_hz < kMinFrequency || c.frequency_hz > kMaxFrequency) {
            limited = true;
            gain = 0.0;
        }
        if (policy.measured_f3_hz > 0.0 && c.frequency_hz < policy.measured_f3_hz && gain > 0.0) {
            limited = true;
            gain = 0.0;
        }
        if (gain > policy.max_boost_db) {
            limited = true;
            gain = policy.max_boost_db;
        }
        result.limited = result.limited || limited;
        result.points.push_back(CompensationPoint{c.frequency_hz, gain, limited});
    }

    result.diagnostic = result.limited ? "curve limited by policy or valid ISO range" : "ok";
    return result;
}

}  // namespace hibiki

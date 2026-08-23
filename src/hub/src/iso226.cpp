#include "hibiki/iso226.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {
namespace {

constexpr double kOneKhz = 1000.0;
constexpr double kMinFrequency = 20.0;
constexpr double kMaxFrequency = 12500.0;
constexpr double kNormativeFrequencySplit = 4000.0;
constexpr double kMinPhon = 20.0;
constexpr double kHighBandMaxPhon = 80.0;
constexpr double kLowBandMaxPhon = 90.0;

bool normative_phon_for_frequency(const double frequency_hz,
                                  const double phon) noexcept {
    if (!std::isfinite(frequency_hz) || frequency_hz < kMinFrequency ||
        frequency_hz > kMaxFrequency || !std::isfinite(phon)) {
        return false;
    }
    const bool high_band = frequency_hz > kNormativeFrequencySplit;
    return phon >= kMinPhon && phon <= (high_band ? kHighBandMaxPhon : kLowBandMaxPhon);
}

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
        policy.measured_f3_hz < 0.0 || policy.measured_f3_hz > 20000.0) {
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

bool iso226_spl_from_phon(const Iso226FormulaPointV1& point,
                          const Iso226FormulaReferenceV1& reference,
                          const double phon,
                          double& spl_db) noexcept {
    if (!std::isfinite(point.frequency_hz) || point.frequency_hz < 20.0 ||
        point.frequency_hz > 12500.0 || !std::isfinite(point.alpha_f) || point.alpha_f <= 0.0 ||
        !std::isfinite(point.threshold_db) || !std::isfinite(point.transfer_db) ||
        !std::isfinite(reference.reference_alpha) || reference.reference_alpha <= 0.0 ||
        !std::isfinite(reference.reference_threshold_db) ||
        !normative_phon_for_frequency(point.frequency_hz, phon)) {
        return false;
    }
    const double loudness_term =
        std::pow(10.0, reference.reference_alpha * phon / 10.0) -
        std::pow(10.0, reference.reference_alpha * reference.reference_threshold_db / 10.0);
    const double transfer_term = std::pow(
        4.0e-10, reference.reference_alpha - point.alpha_f) * loudness_term;
    const double threshold_term =
        std::pow(10.0, point.alpha_f * (point.threshold_db + point.transfer_db) / 10.0);
    const double inside = transfer_term + threshold_term;
    if (!std::isfinite(inside) || inside <= 0.0) {
        return false;
    }
    spl_db = (10.0 / point.alpha_f) * std::log10(inside) - point.transfer_db;
    return std::isfinite(spl_db);
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

CompensationResult build_formula_compensation(
    const std::span<const Iso226FormulaPointV1> points,
    const double current_phon,
    const EqualLoudnessPolicyV1& policy) noexcept {
    CompensationResult result;
    if (!validate_policy(policy) || !std::isfinite(current_phon) || current_phon < 20.0 ||
        current_phon > 90.0 || points.empty()) {
        result.diagnostic = "invalid ISO formula compensation inputs";
        return result;
    }

    const Iso226FormulaPointV1* one_khz = nullptr;
    for (const auto& point : points) {
        if (std::abs(point.frequency_hz - kOneKhz) < 1e-6) {
            one_khz = &point;
            break;
        }
    }
    if (one_khz == nullptr) {
        result.diagnostic = "ISO formula points must contain a 1 kHz anchor";
        return result;
    }

    Iso226FormulaReferenceV1 reference{};
    reference.reference_alpha = one_khz->alpha_f;
    reference.reference_threshold_db = one_khz->threshold_db;
    double current_1khz = 0.0;
    double reference_1khz = 0.0;
    if (!iso226_spl_from_phon(*one_khz, reference, current_phon, current_1khz) ||
        !iso226_spl_from_phon(*one_khz, reference, policy.reference_phon, reference_1khz)) {
        result.diagnostic = "ISO formula failed at the 1 kHz anchor";
        return result;
    }

    try {
        result.points.reserve(points.size());
        for (const auto& point : points) {
            double current_spl = 0.0;
            double reference_spl = 0.0;
            if (!iso226_spl_from_phon(point, reference, current_phon, current_spl) ||
                !iso226_spl_from_phon(point, reference, policy.reference_phon, reference_spl)) {
                result.points.clear();
                result.diagnostic = "ISO formula point is outside the validated domain";
                return result;
            }
            bool limited = false;
            double gain = policy.strength *
                ((current_spl - current_1khz) - (reference_spl - reference_1khz));
            if (point.frequency_hz < kMinFrequency || point.frequency_hz > kMaxFrequency) {
                gain = 0.0;
                limited = true;
            }
            if (policy.measured_f3_hz > 0.0 && point.frequency_hz < policy.measured_f3_hz &&
                gain > 0.0) {
                gain = 0.0;
                limited = true;
            }
            if (gain > policy.max_boost_db) {
                gain = policy.max_boost_db;
                limited = true;
            }
            result.limited = result.limited || limited;
            result.points.push_back(CompensationPoint{point.frequency_hz, gain, limited});
        }
    } catch (...) {
        result.points.clear();
        result.limited = false;
        result.diagnostic = "ISO formula compensation allocation failed";
        return result;
    }
    result.diagnostic = result.limited ? "curve limited by policy or safety range" : "ok";
    return result;
}

}  // namespace hibiki

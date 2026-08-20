// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/calibration_compiler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {
namespace {

constexpr std::size_t kMaxResponsePoints = 512U;
constexpr std::size_t kMaxFilters = 16U;
constexpr double kMinQ = 0.1;
constexpr double kMaxQ = 100.0;

double correction_db(const CalibrationResponsePointV1& point) noexcept {
    return point.target_db - point.measured_db;
}

double distance_octaves(const double left_hz, const double right_hz) noexcept {
    return std::abs(std::log2(left_hz / right_hz));
}

double q_for_point(const std::span<const CalibrationResponsePointV1> response,
                   const std::size_t index,
                   const CalibrationCompilePolicyV1& policy) noexcept {
    const auto& point = response[index];
    double lower = index == 0U
                       ? distance_octaves(response[std::min(index + 1U, response.size() - 1U)].frequency_hz,
                                           point.frequency_hz)
                       : distance_octaves(point.frequency_hz, response[index - 1U].frequency_hz);
    double upper = index + 1U >= response.size()
                       ? lower
                       : distance_octaves(response[index + 1U].frequency_hz, point.frequency_hz);
    const auto bandwidth = std::clamp((lower + upper) * 0.5, 0.125, 2.0);
    const auto q = 1.0 / (2.0 * std::sinh(std::log(2.0) * bandwidth * 0.5));
    return std::clamp(q, policy.min_q, policy.max_q);
}

bool close_to_selected(const std::span<const CalibrationResponsePointV1> response,
                       const std::span<const std::size_t> selected,
                       const std::size_t candidate,
                       const double spacing) noexcept {
    for (const auto index : selected) {
        if (distance_octaves(response[index].frequency_hz,
                             response[candidate].frequency_hz) < spacing) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool validate_calibration_compile_policy_v1(
    const CalibrationCompilePolicyV1& policy) noexcept {
    return policy.schema_version == 1U && policy.max_filters > 0U &&
           policy.max_filters <= kMaxFilters && std::isfinite(policy.min_frequency_hz) &&
           std::isfinite(policy.max_frequency_hz) && policy.min_frequency_hz >= 10.0 &&
           policy.max_frequency_hz <= 24000.0 &&
           policy.min_frequency_hz < policy.max_frequency_hz && std::isfinite(policy.max_boost_db) &&
           policy.max_boost_db >= 0.0 && policy.max_boost_db <= 24.0 &&
           std::isfinite(policy.max_cut_db) && policy.max_cut_db >= 0.0 &&
           policy.max_cut_db <= 44.0 && std::isfinite(policy.min_q) &&
           std::isfinite(policy.max_q) && policy.min_q >= kMinQ && policy.max_q <= kMaxQ &&
           policy.min_q <= policy.max_q && std::isfinite(policy.min_spacing_octaves) &&
           policy.min_spacing_octaves >= 0.01 && policy.min_spacing_octaves <= 4.0 &&
           std::isfinite(policy.ignore_error_db) && policy.ignore_error_db >= 0.0 &&
           policy.ignore_error_db <= 12.0;
}

bool validate_calibration_response_v1(
    const std::span<const CalibrationResponsePointV1> response,
    const CalibrationCompilePolicyV1& policy) noexcept {
    if (!validate_calibration_compile_policy_v1(policy) || response.empty() ||
        response.size() > kMaxResponsePoints) {
        return false;
    }
    double previous_frequency = 0.0;
    for (const auto& point : response) {
        if (!std::isfinite(point.frequency_hz) || !std::isfinite(point.measured_db) ||
            !std::isfinite(point.target_db) || point.frequency_hz < policy.min_frequency_hz ||
            point.frequency_hz > policy.max_frequency_hz ||
            (previous_frequency > 0.0 && point.frequency_hz <= previous_frequency)) {
            return false;
        }
        previous_frequency = point.frequency_hz;
    }
    return true;
}

CalibrationCompileResultV1 compile_bounded_peq_correction_v1(
    const std::span<const CalibrationResponsePointV1> response,
    const CalibrationCompilePolicyV1& policy) {
    CalibrationCompileResultV1 result;
    if (!validate_calibration_response_v1(response, policy)) {
        result.diagnostic = "invalid calibration response or compile policy";
        return result;
    }

    result.maximum_requested_correction_db = 0.0;
    for (const auto& point : response) {
        result.maximum_requested_correction_db = std::max(
            result.maximum_requested_correction_db, std::abs(correction_db(point)));
    }

    std::vector<std::size_t> candidates;
    candidates.reserve(response.size());
    for (std::size_t index = 0U; index < response.size(); ++index) {
        if (std::abs(correction_db(response[index])) > policy.ignore_error_db) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        result.diagnostic = "response is within ignore threshold; no PEQ required";
        return result;
    }

    std::vector<std::size_t> selected;
    selected.reserve(policy.max_filters);
    while (selected.size() < policy.max_filters && selected.size() < candidates.size()) {
        std::size_t best = std::numeric_limits<std::size_t>::max();
        double best_error = -1.0;
        for (const auto candidate : candidates) {
            if (std::find(selected.begin(), selected.end(), candidate) != selected.end() ||
                close_to_selected(response, selected, candidate, policy.min_spacing_octaves)) {
                continue;
            }
            const auto error = std::abs(correction_db(response[candidate]));
            if (error > best_error) {
                best_error = error;
                best = candidate;
            }
        }
        if (best == std::numeric_limits<std::size_t>::max()) break;
        selected.push_back(best);
    }

    if (selected.size() < candidates.size()) result.limited = true;
    std::sort(selected.begin(), selected.end());
    result.filters.reserve(selected.size());
    for (const auto index : selected) {
        const auto requested = correction_db(response[index]);
        const auto gain = std::clamp(requested, -policy.max_cut_db, policy.max_boost_db);
        if (std::abs(gain - requested) > 1e-12) result.limited = true;
        result.filters.push_back(PeqFilterV1{
            response[index].frequency_hz, gain, q_for_point(response, index, policy)});
    }
    result.diagnostic = result.limited
                            ? "bounded PEQ compiled; review clipped or unrepresented residuals"
                            : "bounded PEQ compiled; verify with a second measurement";
    return result;
}

}  // namespace hibiki

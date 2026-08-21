// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/program_loudness.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

constexpr double kMinEnergy = 1.0e-14;
constexpr double kPi = 3.14159265358979323846;
constexpr double kKWeightHighPassHz = 38.1358;
constexpr double kKWeightShelfHz = 1681.974;
constexpr double kKWeightShelfDb = 4.0;

double db_from_energy(const double energy) noexcept {
    if (!std::isfinite(energy) || energy <= kMinEnergy) return -144.0;
    return std::clamp(10.0 * std::log10(energy), -144.0, 24.0);
}

ProgramAwareLevelControllerV1::Biquad make_high_pass(const std::uint32_t sample_rate) noexcept {
    const auto omega = 2.0 * kPi * kKWeightHighPassHz /
                       static_cast<double>(sample_rate);
    const auto cosine = std::cos(omega);
    const auto sine = std::sin(omega);
    const auto alpha = sine;  // Q = 0.5, so sin(w)/(2Q) == sin(w).
    const auto a0 = 1.0 + alpha;
    return ProgramAwareLevelControllerV1::Biquad{
        (1.0 + cosine) / (2.0 * a0),
        -(1.0 + cosine) / a0,
        (1.0 + cosine) / (2.0 * a0),
        (-2.0 * cosine) / a0,
        (1.0 - alpha) / a0,
    };
}

ProgramAwareLevelControllerV1::Biquad make_high_shelf(const std::uint32_t sample_rate) noexcept {
    const auto omega = 2.0 * kPi * kKWeightShelfHz /
                       static_cast<double>(sample_rate);
    const auto cosine = std::cos(omega);
    const auto sine = std::sin(omega);
    const auto amplitude = std::pow(10.0, kKWeightShelfDb / 40.0);
    const auto alpha = sine / std::sqrt(2.0);  // shelf slope S = 1.
    const auto beta = 2.0 * std::sqrt(amplitude) * alpha;
    const auto a0 = (amplitude + 1.0) + (amplitude - 1.0) * cosine + beta;
    const auto b0 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine + beta);
    const auto b1 = 2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
    const auto b2 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine - beta);
    const auto a1 = -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
    const auto a2 = (amplitude + 1.0) + (amplitude - 1.0) * cosine - beta;
    return ProgramAwareLevelControllerV1::Biquad{b0 / a0, b1 / a0, b2 / a0, a1 / a0,
                                                  a2 / a0};
}

double process_biquad(const ProgramAwareLevelControllerV1::Biquad& section,
                      ProgramAwareLevelControllerV1::BiquadState& state,
                      const double input) noexcept {
    const auto output = section.b0 * input + section.b1 * state.x1 + section.b2 * state.x2 -
                        section.a1 * state.y1 - section.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = std::isfinite(output) ? output : 0.0;
    return state.y1;
}

}  // namespace

bool validate_program_aware_policy(const ProgramAwareLevelPolicyV1& policy) noexcept {
    return policy.schema_version == 1U &&
           (policy.meter_mode == ProgramAwareMeterModeV1::RmsProxy ||
            policy.meter_mode == ProgramAwareMeterModeV1::KWeightedProxy) &&
           policy.excluded_channel >= -1 && policy.excluded_channel <= 7 &&
           std::isfinite(policy.target_dbfs) &&
           policy.target_dbfs >= -60.0 && policy.target_dbfs <= 0.0 &&
           std::isfinite(policy.max_boost_db) && policy.max_boost_db >= 0.0 &&
           policy.max_boost_db <= 12.0 && std::isfinite(policy.max_cut_db) &&
           policy.max_cut_db >= 0.0 && policy.max_cut_db <= 24.0 &&
           std::isfinite(policy.analysis_window_ms) && policy.analysis_window_ms >= 100.0 &&
           policy.analysis_window_ms <= 10000.0 &&
           std::isfinite(policy.max_rate_db_per_second) &&
           policy.max_rate_db_per_second > 0.0 && policy.max_rate_db_per_second <= 60.0 &&
           std::isfinite(policy.silence_gate_dbfs) && policy.silence_gate_dbfs >= -144.0 &&
           policy.silence_gate_dbfs < 0.0;
}

bool ProgramAwareLevelControllerV1::configure(const ProgramAwareLevelPolicyV1& policy,
                                              const std::uint32_t sample_rate) noexcept {
    if (!validate_program_aware_policy(policy) || sample_rate < 8000U || sample_rate > 192000U) {
        return false;
    }
    policy_ = policy;
    sample_rate_ = sample_rate;
    k_weighting_[0] = make_high_pass(sample_rate);
    k_weighting_[1] = make_high_shelf(sample_rate);
    configured_ = true;
    reset();
    status_.enabled = policy_.enabled;
    return true;
}

void ProgramAwareLevelControllerV1::reset() noexcept {
    smoothed_energy_ = 0.0;
    status_ = {};
    status_.schema_version = 1U;
    status_.enabled = policy_.enabled;
    status_.silence_gated = true;
    status_.meter_mode = policy_.meter_mode;
    for (auto& bank : k_state_) {
        for (auto& state : bank) state = {};
    }
}

bool ProgramAwareLevelControllerV1::process_interleaved(float* const interleaved,
                                                        const std::size_t frames,
                                                        const std::uint32_t channels) noexcept {
    if (!configured_ || interleaved == nullptr || frames == 0U || channels == 0U ||
        channels > 8U) {
        return false;
    }

    double energy_sum = 0.0;
    std::size_t finite_samples = 0U;
    const auto samples = frames * static_cast<std::size_t>(channels);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            auto& sample_ref = interleaved[frame * channels + channel];
            const auto input = std::isfinite(sample_ref) ? static_cast<double>(sample_ref) : 0.0;
            if (!std::isfinite(sample_ref)) sample_ref = 0.0F;
            if (policy_.excluded_channel == static_cast<std::int32_t>(channel)) continue;
            auto measured = input;
            if (policy_.meter_mode == ProgramAwareMeterModeV1::KWeightedProxy) {
                measured = process_biquad(k_weighting_[0], k_state_[0][channel], measured);
                measured = process_biquad(k_weighting_[1], k_state_[1][channel], measured);
            }
            if (std::isfinite(measured)) {
                energy_sum += measured * measured;
                ++finite_samples;
            }
        }
    }

    const auto block_energy = finite_samples == 0U
                                  ? 0.0
                                  : energy_sum / static_cast<double>(finite_samples);
    const auto window_seconds = policy_.analysis_window_ms / 1000.0;
    const auto block_seconds = static_cast<double>(frames) / static_cast<double>(sample_rate_);
    const auto alpha = std::clamp(1.0 - std::exp(-block_seconds / window_seconds), 0.0, 1.0);
    if (smoothed_energy_ <= 0.0) {
        smoothed_energy_ = block_energy;
    } else {
        smoothed_energy_ += alpha * (block_energy - smoothed_energy_);
    }

    status_.schema_version = 1U;
    status_.valid = true;
    status_.enabled = policy_.enabled;
    status_.meter_mode = policy_.meter_mode;
    status_.measured_dbfs = db_from_energy(smoothed_energy_);
    status_.silence_gated = status_.measured_dbfs <= policy_.silence_gate_dbfs;
    status_.desired_gain_db = 0.0;
    if (policy_.enabled && !status_.silence_gated) {
        status_.desired_gain_db = std::clamp(policy_.target_dbfs - status_.measured_dbfs,
                                             -policy_.max_cut_db, policy_.max_boost_db);
    }

    const auto max_step = policy_.max_rate_db_per_second * block_seconds;
    const auto delta = std::clamp(status_.desired_gain_db - status_.applied_gain_db,
                                  -max_step, max_step);
    status_.applied_gain_db = std::clamp(status_.applied_gain_db + delta,
                                         -policy_.max_cut_db, policy_.max_boost_db);
    const auto linear_gain = static_cast<float>(
        std::pow(10.0, std::clamp(status_.applied_gain_db, -144.0, 12.0) / 20.0));
    for (std::size_t index = 0U; index < samples; ++index) {
        interleaved[index] *= linear_gain;
    }
    return true;
}

}  // namespace hibiki

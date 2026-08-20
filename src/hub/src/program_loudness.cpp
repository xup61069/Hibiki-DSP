// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/program_loudness.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

constexpr double kMinEnergy = 1.0e-14;

double db_from_energy(const double energy) noexcept {
    if (!std::isfinite(energy) || energy <= kMinEnergy) return -144.0;
    return std::clamp(10.0 * std::log10(energy), -144.0, 24.0);
}

}  // namespace

bool validate_program_aware_policy(const ProgramAwareLevelPolicyV1& policy) noexcept {
    return policy.schema_version == 1U && std::isfinite(policy.target_dbfs) &&
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
    for (std::size_t index = 0U; index < samples; ++index) {
        const auto sample = static_cast<double>(interleaved[index]);
        if (std::isfinite(sample)) {
            energy_sum += sample * sample;
            ++finite_samples;
        } else {
            interleaved[index] = 0.0F;
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

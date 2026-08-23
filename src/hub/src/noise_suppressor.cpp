// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/noise_suppressor.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {

float one_pole_coeff(const double milliseconds, const std::uint32_t sample_rate) noexcept {
    const auto seconds = std::max(0.0001, milliseconds / 1000.0);
    return static_cast<float>(1.0 - std::exp(-1.0 /
                                               (seconds * static_cast<double>(sample_rate))));
}

}  // namespace

bool validate_noise_suppressor_policy(const BasicNoiseSuppressorPolicyV1& policy) noexcept {
    return policy.schema_version == 1U && std::isfinite(policy.threshold_dbfs) &&
           policy.threshold_dbfs >= -96.0 && policy.threshold_dbfs < 0.0 &&
           std::isfinite(policy.floor_db) && policy.floor_db >= -96.0 && policy.floor_db <= 0.0 &&
           std::isfinite(policy.attack_ms) && policy.attack_ms >= 0.1 &&
           policy.attack_ms <= 2000.0 && std::isfinite(policy.release_ms) &&
           policy.release_ms >= 1.0 && policy.release_ms <= 5000.0 &&
           std::isfinite(policy.highpass_hz) && policy.highpass_hz >= 0.0 &&
           policy.highpass_hz <= 2000.0;
}

bool BasicNoiseSuppressorV1::configure(const BasicNoiseSuppressorPolicyV1& policy,
                                       const std::uint32_t sample_rate,
                                       const std::uint32_t channels) noexcept {
    if (!validate_noise_suppressor_policy(policy) || sample_rate < 8000U ||
        sample_rate > 192000U || channels == 0U || channels > 8U ||
        policy.highpass_hz >= static_cast<double>(sample_rate) / 2.0) {
        return false;
    }
    policy_ = policy;
    sample_rate_ = sample_rate;
    channels_ = channels;
    threshold_linear_ = static_cast<float>(std::pow(10.0, policy.threshold_dbfs / 20.0));
    // Upper-only hysteresis: the gate closes at the configured threshold
    // (identical to prior behavior) but requires a 2 dB higher envelope to
    // reopen. This prevents chatter when the signal hovers near threshold.
    threshold_open_linear_ =
        static_cast<float>(threshold_linear_ * std::pow(10.0, 2.0 / 20.0));
    floor_linear_ = static_cast<float>(std::pow(10.0, policy.floor_db / 20.0));
    highpass_alpha_ = policy.highpass_hz <= 0.0
                          ? 0.0F
                          : static_cast<float>(std::exp(
                                -2.0 * 3.14159265358979323846 * policy.highpass_hz /
                                static_cast<double>(sample_rate)));
    envelope_attack_coeff_ = one_pole_coeff(policy.attack_ms, sample_rate);
    envelope_release_coeff_ = one_pole_coeff(policy.release_ms, sample_rate);
    gain_attack_coeff_ = one_pole_coeff(policy.attack_ms, sample_rate);
    gain_release_coeff_ = one_pole_coeff(policy.release_ms, sample_rate);
    configured_ = true;
    reset();
    return true;
}

void BasicNoiseSuppressorV1::reset() noexcept {
    envelope_.fill(0.0F);
    gain_.fill(1.0F);
    gate_open_.fill(false);
    previous_input_.fill(0.0F);
    highpass_state_.fill(0.0F);
}

bool BasicNoiseSuppressorV1::process_interleaved(float* const interleaved,
                                                 const std::size_t frames) noexcept {
    if (!configured_ || interleaved == nullptr || frames == 0U) return false;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels_; ++channel) {
            const auto index = frame * channels_ + channel;
            auto input = interleaved[index];
            if (!std::isfinite(input)) input = 0.0F;
            auto filtered = input;
            if (highpass_alpha_ > 0.0F) {
                filtered = highpass_alpha_ *
                           (highpass_state_[channel] + input - previous_input_[channel]);
                highpass_state_[channel] = std::isfinite(filtered) ? filtered : 0.0F;
                previous_input_[channel] = input;
            }
            const auto magnitude = std::abs(filtered);
            const auto envelope_coeff = magnitude > envelope_[channel]
                                            ? envelope_attack_coeff_
                                            : envelope_release_coeff_;
            envelope_[channel] += envelope_coeff * (magnitude - envelope_[channel]);
            // Upper-only hysteresis prevents chatter: close at the configured
            // threshold, reopen only 2 dB higher; hold between.
            if (!gate_open_[channel] && envelope_[channel] >= threshold_open_linear_) {
                gate_open_[channel] = true;
            } else if (gate_open_[channel] && envelope_[channel] < threshold_linear_) {
                gate_open_[channel] = false;
            }
            const auto desired_gain = gate_open_[channel] ? 1.0F : floor_linear_;
            // Attack (attack_ms) controls how fast the gate OPENS as the signal
            // rises above threshold; release (release_ms) controls how fast it
            // CLOSES after the signal falls below threshold.
            const auto gate_opening = desired_gain > gain_[channel];
            const auto gain_coeff = gate_opening ? gain_attack_coeff_
                                                 : gain_release_coeff_;
            gain_[channel] += gain_coeff * (desired_gain - gain_[channel]);
            const auto output = filtered * gain_[channel];
            interleaved[index] = std::isfinite(output) ? output : 0.0F;
        }
    }
    return true;
}

}  // namespace hibiki

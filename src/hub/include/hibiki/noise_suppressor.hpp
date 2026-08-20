#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>

namespace hibiki {

struct BasicNoiseSuppressorPolicyV1 {
    std::uint32_t schema_version{1};
    bool enabled{false};
    double threshold_dbfs{-55.0};
    double floor_db{-24.0};
    double attack_ms{8.0};
    double release_ms{120.0};
    double highpass_hz{80.0};
};

[[nodiscard]] bool validate_noise_suppressor_policy(
    const BasicNoiseSuppressorPolicyV1& policy) noexcept;

// A deterministic, bounded high-pass plus downward-gate processor. It is
// intentionally not an ML/spectral denoiser and must be labelled accordingly.
class BasicNoiseSuppressorV1 final {
public:
    [[nodiscard]] bool configure(const BasicNoiseSuppressorPolicyV1& policy,
                                 std::uint32_t sample_rate,
                                 std::uint32_t channels) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process_interleaved(float* interleaved,
                                           std::size_t frames) noexcept;
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] bool configured() const noexcept { return configured_; }

private:
    BasicNoiseSuppressorPolicyV1 policy_{};
    std::array<float, 8U> envelope_{};
    std::array<float, 8U> gain_{};
    std::array<float, 8U> previous_input_{};
    std::array<float, 8U> highpass_state_{};
    std::uint32_t sample_rate_{0U};
    std::uint32_t channels_{0U};
    float highpass_alpha_{0.0F};
    float threshold_linear_{0.0F};
    float floor_linear_{1.0F};
    float envelope_attack_coeff_{1.0F};
    float envelope_release_coeff_{1.0F};
    float gain_attack_coeff_{1.0F};
    float gain_release_coeff_{1.0F};
    bool configured_{false};
};

}  // namespace hibiki

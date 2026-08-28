#include "hibiki/true_peak_limiter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {
namespace {

constexpr double kMaxRecoveryDbPerMs{6.0206};

float recovery_gain_for_block(const float applied_gain,
                              const std::size_t frames,
                              const std::uint32_t sample_rate) noexcept {
    if (applied_gain >= 1.0F || frames == 0U || sample_rate == 0U) return applied_gain;
    const auto block_seconds = static_cast<double>(frames) /
                               static_cast<double>(sample_rate);
    const auto max_recovery_linear =
        std::pow(10.0, kMaxRecoveryDbPerMs * block_seconds * 1000.0 / 20.0);
    return static_cast<float>(std::min(1.0, static_cast<double>(applied_gain) *
                                                max_recovery_linear));
}

}  // namespace

void TruePeakLimiterV1::reset() noexcept {
    previous_.fill(0.0F);
    has_previous_ = false;
    applied_gain_ = 1.0F;
}

float TruePeakLimiterV1::limit_in_place(float* const interleaved,
                                        const std::size_t frames,
                                        const std::uint32_t channels,
                                        const double ceiling_dbtp,
                                        const std::uint32_t sample_rate) noexcept {
    if (interleaved == nullptr || frames == 0U || channels == 0U || channels > 8U ||
        !std::isfinite(ceiling_dbtp) || sample_rate < 8000U || sample_rate > 192000U) {
        return 1.0F;
    }
    if (frames > std::numeric_limits<std::size_t>::max() /
                    static_cast<std::size_t>(channels)) {
        return 1.0F;
    }
    const auto ceiling = static_cast<float>(std::pow(10.0, std::clamp(ceiling_dbtp, -144.0, 0.0) / 20.0));
    // Keep the peak scan in double so a finite transition between opposite
    // extreme float samples cannot overflow the interpolation difference.
    double peak = 0.0;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            auto& sample = interleaved[frame * channels + channel];
            if (!std::isfinite(sample)) sample = 0.0F;
            const auto current = static_cast<double>(sample);
            peak = std::max(peak, std::abs(current));
            if (has_previous_) {
                const auto prior = static_cast<double>(previous_[channel]);
                for (std::uint32_t step = 1U; step <= 3U; ++step) {
                    const auto fraction = static_cast<double>(step) * 0.25;
                    peak = std::max(peak, std::abs(prior + (current - prior) * fraction));
                }
            }
            previous_[channel] = sample;
        }
        has_previous_ = true;
    }
    const auto required_gain =
        (peak <= static_cast<double>(ceiling) || peak <= 0.0) ? 1.0F
                                                               : static_cast<float>(ceiling / peak);
    const auto gain = required_gain >= applied_gain_
                          ? std::min(required_gain,
                                     recovery_gain_for_block(applied_gain_,
                                                             frames,
                                                             sample_rate))
                          : required_gain;
    if (!std::isfinite(gain) || gain <= 0.0F) {
        // A very small but valid ceiling can make the representable float
        // gain zero. Assign zero explicitly: multiplying FLT_MAX by zero
        // would otherwise create NaN even though the input was finite.
        std::fill_n(interleaved, frames * channels, 0.0F);
    } else {
        for (std::size_t index = 0U; index < frames * channels; ++index) {
            interleaved[index] *= gain;
        }
    }
    for (std::uint32_t channel = 0U; channel < channels; ++channel) {
        previous_[channel] *= gain;
    }
    applied_gain_ = gain;
    return gain;
}

}  // namespace hibiki

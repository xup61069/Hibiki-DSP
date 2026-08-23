#include "hibiki/true_peak_limiter.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

void TruePeakLimiterV1::reset() noexcept {
    previous_.fill(0.0F);
    has_previous_ = false;
    applied_gain_ = 1.0F;
}

float TruePeakLimiterV1::limit_in_place(float* const interleaved,
                                        const std::size_t frames,
                                        const std::uint32_t channels,
                                        const double ceiling_dbtp) noexcept {
    if (interleaved == nullptr || frames == 0U || channels == 0U || channels > 8U ||
        !std::isfinite(ceiling_dbtp)) {
        return 1.0F;
    }
    const auto ceiling = static_cast<float>(std::pow(10.0, std::clamp(ceiling_dbtp, -144.0, 0.0) / 20.0));
    float peak = 0.0F;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            auto& sample = interleaved[frame * channels + channel];
            if (!std::isfinite(sample)) sample = 0.0F;
            const auto current = sample;
            peak = std::max(peak, std::abs(current));
            if (has_previous_) {
                const auto prior = previous_[channel];
                for (std::uint32_t step = 1U; step <= 3U; ++step) {
                    const auto fraction = static_cast<float>(step) * 0.25F;
                    peak = std::max(peak, std::abs(prior + (current - prior) * fraction));
                }
            }
            previous_[channel] = current;
        }
        has_previous_ = true;
    }
    const auto required_gain =
        (peak <= ceiling || peak <= 0.0F) ? 1.0F : ceiling / peak;
    const auto gain = required_gain >= applied_gain_
                          ? std::min(required_gain,
                                     applied_gain_ * kMaxRecoveryGainPerBlock)
                          : required_gain;
    for (std::size_t index = 0U; index < frames * channels; ++index) {
        interleaved[index] *= gain;
    }
    for (std::uint32_t channel = 0U; channel < channels; ++channel) {
        previous_[channel] *= gain;
    }
    applied_gain_ = gain;
    return gain;
}

}  // namespace hibiki

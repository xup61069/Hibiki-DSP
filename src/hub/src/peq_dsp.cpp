// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/peq_dsp.hpp"

#include <cmath>

namespace hibiki {

bool PeqProcessorV1::prepare(const std::span<const PeqFilterV1> filters,
                             const std::uint32_t sample_rate,
                             const std::uint32_t channels) noexcept {
    if (sample_rate < 8000U || sample_rate > 192000U || channels == 0U || channels > 8U ||
        filters.size() > kMaxRealtimePeqFiltersV1) {
        return false;
    }
    std::array<Section, kMaxRealtimePeqFiltersV1> next_sections{};
    for (std::size_t index = 0U; index < filters.size(); ++index) {
        const auto& filter = filters[index];
        if (!validate_peq_filter(filter) || filter.frequency_hz >= sample_rate / 2.0) {
            return false;
        }
        const auto omega = 2.0 * 3.14159265358979323846 * filter.frequency_hz /
                           static_cast<double>(sample_rate);
        const auto sine = std::sin(omega);
        const auto cosine = std::cos(omega);
        const auto alpha = sine / (2.0 * filter.q);
        const auto amplitude = std::pow(10.0, filter.gain_db / 40.0);
        const auto a0 = 1.0 + alpha / amplitude;
        const auto b0 = (1.0 + alpha * amplitude) / a0;
        const auto b1 = (-2.0 * cosine) / a0;
        const auto b2 = (1.0 - alpha * amplitude) / a0;
        const auto a1 = (-2.0 * cosine) / a0;
        const auto a2 = (1.0 - alpha / amplitude) / a0;
        if (!std::isfinite(b0) || !std::isfinite(b1) || !std::isfinite(b2) ||
            !std::isfinite(a1) || !std::isfinite(a2)) {
            return false;
        }
        next_sections[index] = Section{static_cast<float>(b0), static_cast<float>(b1),
                                       static_cast<float>(b2), static_cast<float>(a1),
                                       static_cast<float>(a2)};
    }
    sections_ = next_sections;
    filter_count_ = filters.size();
    sample_rate_ = sample_rate;
    channels_ = channels;
    prepared_ = true;
    reset();
    return true;
}

void PeqProcessorV1::reset() noexcept {
    for (auto& bank : state_) {
        for (auto& state : bank) state = {};
    }
}

bool PeqProcessorV1::process_interleaved(float* const interleaved,
                                         const std::size_t frames) const noexcept {
    if (!prepared_ || interleaved == nullptr || frames == 0U) return false;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels_; ++channel) {
            auto sample = interleaved[frame * channels_ + channel];
            if (!std::isfinite(sample)) sample = 0.0F;
            for (std::size_t filter = 0U; filter < filter_count_; ++filter) {
                auto& state = state_[filter][channel];
                const auto& section = sections_[filter];
                const auto output = section.b0 * sample + section.b1 * state.x1 +
                                    section.b2 * state.x2 - section.a1 * state.y1 -
                                    section.a2 * state.y2;
                state.x2 = state.x1;
                state.x1 = sample;
                state.y2 = state.y1;
                state.y1 = std::isfinite(output) ? output : 0.0F;
                sample = state.y1;
            }
            interleaved[frame * channels_ + channel] = sample;
        }
    }
    return true;
}

}  // namespace hibiki

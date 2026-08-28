// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_convolver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {

bool IrConvolverV1::prepare(const std::span<const float> kernel,
                            const std::size_t taps,
                            const std::uint32_t kernel_channels,
                            const std::uint32_t channels,
                            const std::uint32_t sample_rate,
                            const IrPhaseResolutionV1& phase) noexcept {
    if (taps == 0U || taps > kMaxRealtimeIrTapsV1 ||
        kernel.size() != taps * static_cast<std::size_t>(kernel_channels) ||
        kernel_channels == 0U || kernel_channels > 8U || channels == 0U || channels > 8U ||
        (kernel_channels != 1U && kernel_channels != channels) || sample_rate < 8000U ||
        sample_rate > 192000U || !phase.valid || !std::isfinite(phase.added_delay_ms) ||
        phase.added_delay_ms < 0.0) {
        return false;
    }
    for (const auto value : kernel) {
        if (!std::isfinite(value)) return false;
    }

    kernel_ = {};
    for (std::uint32_t channel = 0U; channel < kernel_channels; ++channel) {
        std::copy_n(kernel.data() + static_cast<std::size_t>(channel) * taps, taps,
                    kernel_[channel].begin());
    }
    status_ = IrConvolverStatusV1{1U, true, sample_rate, channels, kernel_channels, taps,
                                  phase.added_delay_ms, phase.uses_fir};
    prepared_ = true;
    reset();
    return true;
}

void IrConvolverV1::reset() noexcept {
    for (auto& channel : history_) channel.fill(0.0F);
    write_index_ = 0U;
}

bool IrConvolverV1::process_interleaved(float* const interleaved,
                                        const std::size_t frames,
                                        const std::uint32_t channels) noexcept {
    if (!prepared_ || interleaved == nullptr || frames == 0U || channels == 0U || channels > 8U ||
        channels != status_.channels) {
        return false;
    }
    if (frames > std::numeric_limits<std::size_t>::max() /
                    static_cast<std::size_t>(channels)) {
        return false;
    }
    constexpr auto kHistoryMask = kMaxRealtimeIrTapsV1 - 1U;
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        for (std::uint32_t channel = 0U; channel < channels; ++channel) {
            auto& history = history_[channel];
            auto input = interleaved[frame * channels + channel];
            if (!std::isfinite(input)) input = 0.0F;
            history[write_index_] = input;
            const auto kernel_channel = status_.kernel_channels == 1U ? 0U : channel;
            double sum = 0.0;
            for (std::size_t tap = 0U; tap < status_.taps; ++tap) {
                const auto history_index = (write_index_ + kMaxRealtimeIrTapsV1 - tap) &
                                           kHistoryMask;
                sum += static_cast<double>(kernel_[kernel_channel][tap]) *
                       static_cast<double>(history[history_index]);
            }
            interleaved[frame * channels + channel] =
                std::isfinite(sum) ? static_cast<float>(sum) : 0.0F;
        }
        write_index_ = (write_index_ + 1U) & kHistoryMask;
    }
    return true;
}

}  // namespace hibiki

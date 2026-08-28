// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/output_crossfade.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hibiki {

bool OutputCrossfade::begin(const std::uint32_t channels,
                            const std::uint32_t sample_rate,
                            const std::uint32_t duration_ms) noexcept {
    if ((channels != 2U && channels != 6U && channels != 8U) ||
        (sample_rate != 44100U && sample_rate != 48000U && sample_rate != 96000U &&
         sample_rate != 192000U) ||
        duration_ms == 0U || duration_ms > 200U) {
        return false;
    }
    const auto frames = (static_cast<std::uint64_t>(sample_rate) * duration_ms + 999U) / 1000U;
    if (frames == 0U || frames > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    snapshot_ = OutputCrossfadeSnapshotV1{true, channels, static_cast<std::size_t>(frames), 0U};
    return true;
}

void OutputCrossfade::reset() noexcept { snapshot_ = {}; }

bool OutputCrossfade::process(const float* const old_interleaved,
                              const float* const new_interleaved,
                              float* const output_interleaved,
                              const std::size_t frames) noexcept {
    if (!snapshot_.active || old_interleaved == nullptr || new_interleaved == nullptr ||
        output_interleaved == nullptr || frames == 0U) {
        return false;
    }
    const auto channel_count = static_cast<std::size_t>(snapshot_.channels);
    if (channel_count == 0U ||
        frames > std::numeric_limits<std::size_t>::max() / channel_count ||
        frames > std::numeric_limits<std::size_t>::max() - snapshot_.processed_frames) {
        return false;
    }
    constexpr double half_pi = 1.57079632679489661923;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto absolute = snapshot_.processed_frames + frame;
        const auto bounded = std::min(absolute, snapshot_.total_frames);
        const auto position = static_cast<double>(bounded) /
                              static_cast<double>(snapshot_.total_frames);
        const auto old_gain = static_cast<float>(std::cos(position * half_pi));
        const auto new_gain = static_cast<float>(std::sin(position * half_pi));
        for (std::uint32_t channel = 0; channel < snapshot_.channels; ++channel) {
            const auto index = frame * snapshot_.channels + channel;
            output_interleaved[index] = old_interleaved[index] * old_gain +
                                        new_interleaved[index] * new_gain;
        }
    }
    snapshot_.processed_frames = std::min(snapshot_.total_frames,
                                           snapshot_.processed_frames + frames);
    if (snapshot_.processed_frames >= snapshot_.total_frames) snapshot_.active = false;
    return true;
}

}  // namespace hibiki

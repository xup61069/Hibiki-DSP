#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

// Validate and copy exactly the requested converted sample prefix. The caller
// owns any allocation needed for the destination; this seam only commits
// already-produced samples and never silently accepts a partial/non-finite
// conversion.
[[nodiscard]] inline bool commit_resampled_wav_samples_v1(
    const std::span<const float> converted,
    const std::size_t nominal_frames,
    const std::uint16_t channels,
    const std::span<float> destination) noexcept {
    if (nominal_frames == 0U || channels == 0U) return false;
    if (nominal_frames > (static_cast<std::size_t>(-1) / channels)) return false;
    const auto sample_count = nominal_frames * static_cast<std::size_t>(channels);
    if (converted.size() < sample_count || destination.size() < sample_count) return false;
    for (std::size_t index = 0U; index < sample_count; ++index) {
        if (!std::isfinite(converted[index])) return false;
    }
    std::copy_n(converted.begin(), sample_count, destination.begin());
    return true;
}

}  // namespace hibiki

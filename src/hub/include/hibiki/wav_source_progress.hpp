#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>

namespace hibiki {

// Returns the number of source frames advanced when a looping source's
// position moves from start_frame to next_frame. wrap_count is incremented
// whenever the source resets at total_frames, so short files can wrap more
// than once in a single bounded render block. A reset is a wrap, not a
// negative unsigned delta.
[[nodiscard]] constexpr std::uint64_t wav_source_progress_delta_v1(
    const std::size_t start_frame,
    const std::size_t next_frame,
    const std::size_t total_frames,
    const std::size_t wrap_count) noexcept {
    if (total_frames == 0U || start_frame > total_frames || next_frame > total_frames) {
        return 0U;
    }
    constexpr auto kMax = static_cast<std::uint64_t>(-1);
    if (wrap_count == 0U) {
        if (next_frame < start_frame) return 0U;
        return static_cast<std::uint64_t>(next_frame - start_frame);
    }

    const auto total = static_cast<std::uint64_t>(total_frames);
    const auto tail = static_cast<std::uint64_t>(total_frames - start_frame);
    const auto final_cycle_count = static_cast<std::uint64_t>(wrap_count - 1U);
    if (total != 0U && final_cycle_count > kMax / total) return kMax;
    auto result = final_cycle_count * total;
    if (tail > kMax - result) return kMax;
    result += tail;
    const auto final_position = static_cast<std::uint64_t>(next_frame);
    if (final_position > kMax - result) return kMax;
    return result + final_position;
}

}  // namespace hibiki

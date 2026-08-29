#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_graph.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace hibiki {

// Build the lane view for one bounded offline-render block. The caller owns
// the complete expanded source buffer and supplies an offset in frames; this
// seam makes the per-block pointer advance explicit and testable.
[[nodiscard]] inline bool make_offline_wav_lane_v1(
    const float* lane_data,
    const std::uint32_t channel_count,
    const std::size_t frame_offset,
    RtLaneInputV1& lane) noexcept {
    lane = RtLaneInputV1{};
    if (lane_data == nullptr || channel_count == 0U ||
        frame_offset > (std::numeric_limits<std::size_t>::max() / channel_count)) {
        return false;
    }
    lane = RtLaneInputV1{lane_data + frame_offset * channel_count, channel_count};
    return true;
}

}  // namespace hibiki

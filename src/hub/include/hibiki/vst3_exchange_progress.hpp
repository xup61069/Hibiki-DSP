#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <limits>

namespace hibiki {

struct Vst3ExchangeProgressV1 final {
    std::uint64_t block_start{0U};
    std::uint64_t processed_blocks{0U};
    std::uint64_t tap_sequence{0U};
};

// Commit control-plane progress only after the processed block is accepted by
// the bounded RT lane ring. A rejected block leaves every progress field intact
// so the caller can fail closed without claiming an unqueued exchange.
[[nodiscard]] constexpr bool commit_vst3_exchange_progress_v1(
    const bool lane_push_succeeded,
    const std::size_t frames,
    const std::uint64_t tap_sequence,
    Vst3ExchangeProgressV1& progress) noexcept {
    if (!lane_push_succeeded || frames == 0U ||
        frames > (std::numeric_limits<std::uint64_t>::max)() - progress.block_start) {
        return false;
    }
    progress.block_start += static_cast<std::uint64_t>(frames);
    ++progress.processed_blocks;
    progress.tap_sequence = tap_sequence;
    return true;
}

}  // namespace hibiki

#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>

namespace hibiki {

struct OutputCrossfadeSnapshotV1 {
    bool active{false};
    std::uint32_t channels{0};
    std::size_t total_frames{0};
    std::size_t processed_frames{0};
};

// Audio-side, allocation-free sink handoff. The control plane prepares this
// object before the new sink is committed; the RT caller then renders old and
// new sink blocks into one equal-power transition.
class OutputCrossfade final {
public:
    [[nodiscard]] bool begin(std::uint32_t channels,
                             std::uint32_t sample_rate,
                             std::uint32_t duration_ms = 30) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool process(const float* old_interleaved,
                               const float* new_interleaved,
                               float* output_interleaved,
                               std::size_t frames) noexcept;

    [[nodiscard]] const OutputCrossfadeSnapshotV1& snapshot() const noexcept {
        return snapshot_;
    }

private:
    OutputCrossfadeSnapshotV1 snapshot_{};
};

}  // namespace hibiki

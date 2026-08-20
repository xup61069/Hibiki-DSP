#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace hibiki {

constexpr std::size_t kLaneLatencyMaxLanesV1 = 32U;
constexpr std::uint32_t kLaneLatencyMaxSamplesV1 = 16384U;
constexpr std::size_t kLaneLatencyMaxFramesV1 = 4096U;
constexpr std::size_t kLaneLatencyMaxChannelsV1 = 8U;

struct LaneLatencyConfigV1 {
    std::uint32_t channel_count{0U};
    std::uint32_t delay_samples{0U};
    bool enabled{false};
};

// Delay storage is prepared on the control plane and then only mutated by the
// owning audio callback. A bank is movable so graph Prepare can build a new
// bank before Commit swaps it with the active one.
class LaneLatencyBankV1 final {
public:
    LaneLatencyBankV1() noexcept = default;
    LaneLatencyBankV1(const LaneLatencyBankV1&) = delete;
    LaneLatencyBankV1& operator=(const LaneLatencyBankV1&) = delete;
    LaneLatencyBankV1(LaneLatencyBankV1&&) noexcept = default;
    LaneLatencyBankV1& operator=(LaneLatencyBankV1&&) noexcept = default;
    ~LaneLatencyBankV1() = default;

    [[nodiscard]] bool prepare(std::span<const LaneLatencyConfigV1> configs) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process_lane(std::size_t lane_index,
                                    const float* input_interleaved,
                                    std::uint32_t channels,
                                    std::size_t frames) noexcept;
    [[nodiscard]] const float* output(std::size_t lane_index) const noexcept;
    [[nodiscard]] bool prepared(std::size_t lane_index) const noexcept;
    [[nodiscard]] std::uint32_t delay_samples(std::size_t lane_index) const noexcept;

private:
    struct Slot final {
        std::vector<float> ring;
        std::unique_ptr<float[]> scratch;
        std::uint32_t channels{0U};
        std::uint32_t delay_samples{0U};
        std::size_t ring_length{0U};
        std::size_t write_index{0U};
        bool prepared{false};
    };

    std::array<Slot, kLaneLatencyMaxLanesV1> slots_{};
};

}  // namespace hibiki

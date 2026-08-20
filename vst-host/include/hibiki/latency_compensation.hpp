#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::size_t kLatencyPlanMaxLanesV1 = 32U;
constexpr std::uint32_t kLatencyPlanMaxSamplesV1 = 16384U;
constexpr std::uint32_t kLatencyDelayMaxChannelsV1 = 8U;
constexpr std::uint32_t kLatencyDelayMaxFramesV1 = 4096U;

struct LatencyLaneInputV1 {
    bool active{false};
    std::uint32_t reported_latency_samples{0U};
};

struct LatencyAlignmentPlanV1 {
    std::uint32_t schema_version{1};
    std::uint32_t lane_count{0U};
    std::uint32_t maximum_latency_samples{0U};
    std::array<std::uint32_t, kLatencyPlanMaxLanesV1> delay_samples{};
};

[[nodiscard]] bool build_latency_alignment_plan_v1(
    std::span<const LatencyLaneInputV1> lanes,
    LatencyAlignmentPlanV1& plan) noexcept;

[[nodiscard]] bool validate_latency_alignment_plan_v1(
    const LatencyAlignmentPlanV1& plan) noexcept;

// Caller-owned, fixed-capacity interleaved delay. It is intended for the RT
// lane after a control-plane plan has been committed. No allocation, lock,
// wait or plugin/OS call occurs in prepare/reset/process.
class FixedDelayLineV1 final {
public:
    static constexpr std::size_t kRingSamples =
        static_cast<std::size_t>(kLatencyPlanMaxSamplesV1) + 1U;

    FixedDelayLineV1() noexcept = default;

    [[nodiscard]] bool prepare(std::uint32_t channels,
                               std::uint32_t delay_samples) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(const float* input_interleaved,
                               float* output_interleaved,
                               std::size_t frames) noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::uint32_t delay_samples() const noexcept { return delay_samples_; }
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }

private:
    std::array<std::array<float, kRingSamples>, kLatencyDelayMaxChannelsV1> ring_{};
    std::uint32_t channels_{0U};
    std::uint32_t delay_samples_{0U};
    std::size_t write_index_{0U};
    bool prepared_{false};
};

}  // namespace hibiki

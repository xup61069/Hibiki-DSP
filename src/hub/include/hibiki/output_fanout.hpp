#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace hibiki {

constexpr std::size_t kOutputFanoutMaxSinksV1 = 8U;
constexpr std::size_t kOutputFanoutMaxIdBytesV1 = 64U;

struct OutputFanoutSinkConfigV1 {
    std::string sink_id;
    std::uint32_t channels{2U};
    bool enabled{true};
};

struct OutputFanoutSinkSnapshotV1 {
    std::uint8_t id_bytes{0U};
    std::array<char, kOutputFanoutMaxIdBytesV1> sink_id{};
    std::uint32_t channels{0U};
    bool enabled{false};
};

struct OutputFanoutPlanV1 {
    std::uint32_t schema_version{1U};
    std::uint64_t revision{0U};
    std::uint32_t output_channels{0U};
    std::uint32_t sink_count{0U};
    std::array<OutputFanoutSinkSnapshotV1, kOutputFanoutMaxSinksV1> sinks{};
};

[[nodiscard]] bool prepare_output_fanout_plan_v1(
    std::span<const OutputFanoutSinkConfigV1> configs,
    std::uint32_t output_channels,
    std::uint64_t revision,
    OutputFanoutPlanV1& plan) noexcept;

[[nodiscard]] bool validate_output_fanout_plan_v1(
    const OutputFanoutPlanV1& plan) noexcept;

// Copies one graph block to every enabled sink. All destination pointers and
// capacities are validated before the first write, so a rejected block never
// partially updates the fan-out set.
[[nodiscard]] bool fanout_interleaved_v1(
    const OutputFanoutPlanV1& plan,
    const float* input_interleaved,
    std::size_t frames,
    std::span<float* const> outputs,
    std::span<const std::size_t> output_capacities) noexcept;

}  // namespace hibiki

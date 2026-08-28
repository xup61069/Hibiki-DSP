#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "hibiki/output_sink.hpp"
#include <string>

namespace hibiki {

constexpr std::size_t kOutputFanoutMaxSinksV1 = 8U;
constexpr std::size_t kOutputFanoutMaxIdBytesV1 = 64U;
constexpr std::size_t kOutputFanoutMaxInputFramesV1 = 4096U;
constexpr std::size_t kOutputFanoutMaxResampledFramesV1 =
    (kOutputFanoutMaxInputFramesV1 * 4U) + 1U;

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

struct OutputFanoutRuntimeSnapshotV1 {
    bool prepared{false};
    std::uint32_t output_channels{0U};
    std::uint32_t sink_count{0U};
    std::array<OutputSinkClockSnapshotV1, kOutputFanoutMaxSinksV1> sinks{};
};

// Owns one persistent clock/SRC pipeline per enabled fan-out sink. Scratch is
// allocated once during prepare(); process() performs no allocation, lock or
// wait and only publishes output after every enabled sink succeeds.
class OutputFanoutRuntimeV1 final {
public:
    OutputFanoutRuntimeV1() noexcept = default;
    ~OutputFanoutRuntimeV1() = default;
    OutputFanoutRuntimeV1(const OutputFanoutRuntimeV1&) = delete;
    OutputFanoutRuntimeV1& operator=(const OutputFanoutRuntimeV1&) = delete;

    [[nodiscard]] bool prepare(const OutputFanoutPlanV1& plan,
                               double source_step = 1.0) noexcept;
    void reset() noexcept;
    // Control-side input publishes a bounded latest request. The audio-side
    // process owner applies it before its capacity preflight.
    [[nodiscard]] bool observe_clock(std::size_t sink_index,
                                     double source_frames,
                                     double sink_frames,
                                     double elapsed_seconds) noexcept;
    [[nodiscard]] bool process(const float* input_interleaved,
                               std::size_t input_frames,
                               std::span<float* const> outputs,
                               std::span<const std::size_t> output_capacities,
                               std::span<std::size_t> output_frames) noexcept;
    [[nodiscard]] OutputFanoutRuntimeSnapshotV1 snapshot() const noexcept;

private:
    struct ClockObservationRequest {
        std::atomic<std::uint64_t> sequence{0U};
        std::atomic<double> source_frames{0.0};
        std::atomic<double> sink_frames{0.0};
        std::atomic<double> elapsed_seconds{0.0};
    };

    struct ClockSnapshotPublication {
        std::atomic<std::uint64_t> sequence{0U};
        std::atomic<double> ratio{1.0};
        std::atomic<double> drift_ppm{0.0};
        std::atomic<double> source_step{1.0};
        std::atomic<bool> prepared{false};
    };

    struct ScratchStorage {
        std::array<std::array<float,
                              kOutputFanoutMaxResampledFramesV1 * 8U>,
                   kOutputFanoutMaxSinksV1>
            blocks{};
    };

    OutputFanoutPlanV1 plan_{};
    std::array<OutputSinkModel, kOutputFanoutMaxSinksV1> sinks_{};
    std::unique_ptr<ScratchStorage> scratch_{};
    std::array<ClockObservationRequest, kOutputFanoutMaxSinksV1>
        clock_requests_{};
    std::array<ClockSnapshotPublication, kOutputFanoutMaxSinksV1>
        clock_publications_{};
    std::array<std::uint64_t, kOutputFanoutMaxSinksV1>
        applied_clock_sequences_{};
    bool prepared_{false};

    [[nodiscard]] bool apply_pending_clock_observations() noexcept;
    void publish_clock_snapshot(
        std::size_t sink_index,
        const OutputSinkClockSnapshotV1& snapshot) noexcept;
    [[nodiscard]] bool read_clock_snapshot(
        std::size_t sink_index,
        OutputSinkClockSnapshotV1& snapshot) const noexcept;
};

}  // namespace hibiki

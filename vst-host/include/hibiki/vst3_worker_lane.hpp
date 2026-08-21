#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hibiki/latency_graph_commit.hpp"
#include "hibiki/vst3_parameter_timeline.hpp"
#include "hibiki/vst3_sandbox.hpp"

namespace hibiki {

struct Vst3WorkerLaneConfigV1 {
    std::uint64_t lane_token{0U};
    std::uint32_t channels{2U};
    double sample_rate{48000.0};
    std::uint32_t reported_latency_samples{0U};
    std::uint32_t max_block_frames{kVst3WorkerMaxFramesV1};
};

[[nodiscard]] bool validate_vst3_worker_lane_config_v1(
    const Vst3WorkerLaneConfigV1& config) noexcept;

enum class Vst3WorkerLaneStateV1 : std::uint8_t {
    Detached,
    Prepared,
    Ready,
    Degraded,
};

// Control/IPC-side session bridge. It owns timeline ordering and maps each
// successful block to the bounded worker exchange. The RT graph must consume
// only the resulting caller-owned block; it must never call this class.
class Vst3WorkerLaneSessionV1 final {
public:
    Vst3WorkerLaneSessionV1() noexcept = default;

    [[nodiscard]] bool prepare(Vst3SandboxProcess& sandbox,
                                const Vst3WorkerLaneConfigV1& config) noexcept;
    [[nodiscard]] Vst3WorkerExchangeResultV1 handshake(
        std::uint64_t request_id = 1U);
    void detach() noexcept;

    [[nodiscard]] bool append_parameter_event(
        const Vst3ParameterTimelineEventV1& event) noexcept;
    [[nodiscard]] bool erase_parameter_event(std::size_t index) noexcept;
    [[nodiscard]] bool set_parameter_timeline(
        const Vst3ParameterTimelineSnapshotV1& snapshot) noexcept;
    [[nodiscard]] const Vst3ParameterTimelineSnapshotV1& parameter_timeline() const noexcept {
        return timeline_.snapshot();
    }

    [[nodiscard]] Vst3WorkerExchangeResultV1 process_block(
        std::uint64_t request_id,
        std::uint64_t block_start,
        std::uint32_t frames,
        std::span<const float> input,
        std::span<float> output);

    [[nodiscard]] Vst3WorkerLaneStateV1 state() const noexcept { return state_; }
    [[nodiscard]] LatencyGraphLaneInputV1 latency_lane_input() const noexcept;
    [[nodiscard]] const Vst3WorkerLaneConfigV1& config() const noexcept { return config_; }

private:
    Vst3SandboxProcess* sandbox_{nullptr};
    Vst3WorkerLaneConfigV1 config_{};
    Vst3ParameterTimelineV1 timeline_{};
    Vst3WorkerLaneStateV1 state_{Vst3WorkerLaneStateV1::Detached};
    std::uint64_t next_block_start_{0U};
    bool has_processed_block_{false};
};

}  // namespace hibiki

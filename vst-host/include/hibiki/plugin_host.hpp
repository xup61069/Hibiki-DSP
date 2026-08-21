#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "hibiki/latency_graph_commit.hpp"
#include "hibiki/vst3_worker_lane.hpp"

namespace hibiki {

enum class PluginHostState : std::uint8_t {
    Disabled,
    Starting,
    Running,
    Quarantined,
};

struct PluginDescriptorV1 {
    std::string plugin_id;
    std::uint32_t input_channels{2};
    std::uint32_t output_channels{2};
    std::uint32_t reported_latency_samples{0};
    bool trusted{false};
    std::uint32_t watchdog_timeout_ms{250};
    bool certified{true};
    std::uint64_t lane_token{0U};
};

class PluginHostModel final {
public:
    [[nodiscard]] bool start(const PluginDescriptorV1& descriptor);
    void stop() noexcept;
    void report_crash() noexcept;
    [[nodiscard]] bool heartbeat(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool poll_watchdog(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool can_process() const noexcept;
    [[nodiscard]] bool process_passthrough(const float* input,
                                            float* output,
                                            std::size_t samples) const noexcept;
    [[nodiscard]] bool prepare_worker_session(
        Vst3SandboxProcess& sandbox,
        double sample_rate,
        std::uint32_t max_block_frames) noexcept;
    [[nodiscard]] Vst3WorkerExchangeResultV1 handshake_worker(
        std::uint64_t request_id = 1U);
    [[nodiscard]] Vst3WorkerExchangeResultV1 process_worker_block(
        std::uint64_t request_id,
        std::uint64_t block_start,
        std::uint32_t frames,
        std::span<const float> input,
        std::span<float> output);
    [[nodiscard]] Vst3WorkerLaneStateV1 worker_lane_state() const noexcept {
        return worker_lane_.state();
    }
    [[nodiscard]] LatencyGraphLaneInputV1 worker_latency_lane_input() const noexcept {
        return worker_lane_.latency_lane_input();
    }
    [[nodiscard]] PluginHostState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t latency_samples() const noexcept {
        return descriptor_.reported_latency_samples;
    }
    [[nodiscard]] LatencyGraphLaneInputV1 latency_lane_input() const noexcept {
        return LatencyGraphLaneInputV1{descriptor_.lane_token, can_process(),
                                       descriptor_.output_channels,
                                       descriptor_.reported_latency_samples};
    }

private:
    PluginDescriptorV1 descriptor_{};
    PluginHostState state_{PluginHostState::Disabled};
    std::uint64_t last_heartbeat_ms_{0};
    Vst3WorkerLaneSessionV1 worker_lane_{};
};

}  // namespace hibiki

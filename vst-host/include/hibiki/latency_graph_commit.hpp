#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::size_t kLatencyGraphMaxLanesV1 = 32U;
constexpr std::uint32_t kLatencyGraphMaxSamplesV1 = 16384U;

// A lane token is assigned by the control plane and must remain stable while a
// graph is alive. It is deliberately not a pointer, PID or process-local index.
struct LatencyGraphLaneInputV1 {
    std::uint64_t lane_token{0U};
    bool active{false};
    std::uint32_t channel_count{2U};
    std::uint32_t reported_latency_samples{0U};
};

struct LatencyGraphLaneStateV1 {
    std::uint64_t lane_token{0U};
    bool active{false};
    std::uint32_t channel_count{0U};
    std::uint32_t reported_latency_samples{0U};
    std::uint32_t compensation_delay_samples{0U};
};

struct LatencyGraphCommitV1 {
    std::uint32_t schema_version{1U};
    std::uint64_t base_graph_revision{0U};
    std::uint64_t target_graph_revision{0U};
    std::uint32_t lane_count{0U};
    std::uint32_t maximum_latency_samples{0U};
    std::array<LatencyGraphLaneStateV1, kLatencyGraphMaxLanesV1> lanes{};
};

[[nodiscard]] bool prepare_latency_graph_commit_v1(
    std::span<const LatencyGraphLaneInputV1> lanes,
    std::uint64_t base_graph_revision,
    std::uint64_t target_graph_revision,
    LatencyGraphCommitV1& commit) noexcept;

[[nodiscard]] bool validate_latency_graph_commit_v1(
    const LatencyGraphCommitV1& commit) noexcept;

enum class LatencyGraphTransactionStateV1 : std::uint8_t {
    Ready,
    Prepared,
    Degraded,
};

// Control-plane transaction owner. It never runs on the audio callback and
// only publishes a complete, validated value on commit.
class LatencyGraphCommitterV1 final {
public:
    [[nodiscard]] bool prepare(std::span<const LatencyGraphLaneInputV1> lanes,
                                std::uint64_t target_graph_revision) noexcept;
    [[nodiscard]] bool commit() noexcept;
    void rollback() noexcept;

    [[nodiscard]] LatencyGraphTransactionStateV1 state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t active_graph_revision() const noexcept {
        return active_.target_graph_revision;
    }
    [[nodiscard]] const LatencyGraphCommitV1& active() const noexcept { return active_; }
    [[nodiscard]] const LatencyGraphCommitV1& pending() const noexcept { return pending_; }

private:
    LatencyGraphCommitV1 active_{};
    LatencyGraphCommitV1 pending_{};
    LatencyGraphTransactionStateV1 state_{LatencyGraphTransactionStateV1::Ready};
    bool has_active_{false};
    bool has_pending_{false};
};

}  // namespace hibiki

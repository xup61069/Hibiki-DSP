#include "hibiki/latency_graph_commit.hpp"

#include <algorithm>

namespace hibiki {
namespace {

bool supported_channels(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
}

}  // namespace

bool validate_latency_graph_commit_v1(const LatencyGraphCommitV1& commit) noexcept {
    if (commit.schema_version != 1U || commit.lane_count > kLatencyGraphMaxLanesV1 ||
        commit.target_graph_revision <= commit.base_graph_revision ||
        commit.maximum_latency_samples > kLatencyGraphMaxSamplesV1) {
        return false;
    }

    std::uint32_t maximum = 0U;
    for (std::size_t index = 0U; index < commit.lane_count; ++index) {
        const auto& lane = commit.lanes[index];
        if (lane.lane_token == 0U || !supported_channels(lane.channel_count) ||
            (!lane.active && lane.reported_latency_samples != 0U) ||
            lane.reported_latency_samples > kLatencyGraphMaxSamplesV1 ||
            lane.compensation_delay_samples > kLatencyGraphMaxSamplesV1 ||
            lane.compensation_delay_samples + lane.reported_latency_samples !=
                commit.maximum_latency_samples) {
            return false;
        }
        maximum = std::max(maximum, lane.reported_latency_samples);
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (commit.lanes[prior].lane_token == lane.lane_token) return false;
        }
    }
    return maximum == commit.maximum_latency_samples;
}

bool prepare_latency_graph_commit_v1(
    const std::span<const LatencyGraphLaneInputV1> lanes,
    const std::uint64_t base_graph_revision,
    const std::uint64_t target_graph_revision,
    LatencyGraphCommitV1& commit) noexcept {
    if (lanes.size() > kLatencyGraphMaxLanesV1 ||
        target_graph_revision <= base_graph_revision) {
        return false;
    }

    LatencyGraphCommitV1 candidate{};
    candidate.base_graph_revision = base_graph_revision;
    candidate.target_graph_revision = target_graph_revision;
    candidate.lane_count = static_cast<std::uint32_t>(lanes.size());
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        const auto& source = lanes[index];
        if (source.lane_token == 0U || !supported_channels(source.channel_count) ||
            source.reported_latency_samples > kLatencyGraphMaxSamplesV1) {
            return false;
        }
        auto& target = candidate.lanes[index];
        target.lane_token = source.lane_token;
        target.active = source.active;
        target.channel_count = source.channel_count;
        target.reported_latency_samples = source.active ? source.reported_latency_samples : 0U;
        candidate.maximum_latency_samples =
            std::max(candidate.maximum_latency_samples, target.reported_latency_samples);
    }
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        auto& target = candidate.lanes[index];
        target.compensation_delay_samples = candidate.maximum_latency_samples -
                                            target.reported_latency_samples;
    }
    if (!validate_latency_graph_commit_v1(candidate)) return false;
    commit = candidate;
    return true;
}

bool LatencyGraphCommitterV1::prepare(
    const std::span<const LatencyGraphLaneInputV1> lanes,
    const std::uint64_t target_graph_revision) noexcept {
    const auto base_revision = has_active_ ? active_.target_graph_revision : 0U;
    LatencyGraphCommitV1 candidate{};
    if (!prepare_latency_graph_commit_v1(lanes, base_revision, target_graph_revision, candidate)) {
        state_ = LatencyGraphTransactionStateV1::Degraded;
        has_pending_ = false;
        return false;
    }
    pending_ = candidate;
    has_pending_ = true;
    state_ = LatencyGraphTransactionStateV1::Prepared;
    return true;
}

bool LatencyGraphCommitterV1::commit() noexcept {
    if (!has_pending_ ||
        (has_active_ && pending_.base_graph_revision != active_.target_graph_revision) ||
        (!has_active_ && pending_.base_graph_revision != 0U)) {
        state_ = LatencyGraphTransactionStateV1::Degraded;
        return false;
    }
    active_ = pending_;
    has_active_ = true;
    has_pending_ = false;
    state_ = LatencyGraphTransactionStateV1::Ready;
    return true;
}

void LatencyGraphCommitterV1::rollback() noexcept {
    has_pending_ = false;
    state_ = has_active_ ? LatencyGraphTransactionStateV1::Ready
                         : LatencyGraphTransactionStateV1::Degraded;
}

}  // namespace hibiki

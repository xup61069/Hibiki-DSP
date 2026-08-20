// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/latency_compensation.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

bool validate_latency_alignment_plan_v1(const LatencyAlignmentPlanV1& plan) noexcept {
    if (plan.schema_version != 1U || plan.lane_count > kLatencyPlanMaxLanesV1 ||
        plan.maximum_latency_samples > kLatencyPlanMaxSamplesV1) {
        return false;
    }
    for (std::size_t index = 0U; index < plan.lane_count; ++index) {
        if (plan.delay_samples[index] > plan.maximum_latency_samples) return false;
    }
    for (std::size_t index = plan.lane_count; index < plan.delay_samples.size(); ++index) {
        if (plan.delay_samples[index] != 0U) return false;
    }
    return true;
}

bool build_latency_alignment_plan_v1(const std::span<const LatencyLaneInputV1> lanes,
                                     LatencyAlignmentPlanV1& plan) noexcept {
    LatencyAlignmentPlanV1 candidate{};
    if (lanes.size() > kLatencyPlanMaxLanesV1) return false;
    candidate.lane_count = static_cast<std::uint32_t>(lanes.size());
    for (const auto& lane : lanes) {
        if (lane.reported_latency_samples > kLatencyPlanMaxSamplesV1) return false;
        if (lane.active) {
            candidate.maximum_latency_samples = std::max(
                candidate.maximum_latency_samples, lane.reported_latency_samples);
        }
    }
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        if (lanes[index].active) {
            candidate.delay_samples[index] = candidate.maximum_latency_samples -
                                             lanes[index].reported_latency_samples;
        }
    }
    if (!validate_latency_alignment_plan_v1(candidate)) return false;
    plan = candidate;
    return true;
}

bool FixedDelayLineV1::prepare(const std::uint32_t channels,
                               const std::uint32_t delay_samples) noexcept {
    if (channels == 0U || channels > kLatencyDelayMaxChannelsV1 ||
        delay_samples > kLatencyPlanMaxSamplesV1) {
        prepared_ = false;
        return false;
    }
    channels_ = channels;
    delay_samples_ = delay_samples;
    reset();
    prepared_ = true;
    return true;
}

void FixedDelayLineV1::reset() noexcept {
    for (auto& channel : ring_) channel.fill(0.0F);
    write_index_ = 0U;
}

bool FixedDelayLineV1::process(const float* const input_interleaved,
                               float* const output_interleaved,
                               const std::size_t frames) noexcept {
    if (!prepared_ || input_interleaved == nullptr || output_interleaved == nullptr ||
        frames == 0U || frames > kLatencyDelayMaxFramesV1) {
        return false;
    }
    const auto sample_count = frames * channels_;
    for (std::size_t index = 0U; index < sample_count; ++index) {
        if (!std::isfinite(input_interleaved[index])) {
            std::fill_n(output_interleaved, sample_count, 0.0F);
            reset();
            return false;
        }
    }
    if (delay_samples_ == 0U) {
        std::copy_n(input_interleaved, sample_count, output_interleaved);
        return true;
    }
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const auto read_index = (write_index_ + kRingSamples - delay_samples_) % kRingSamples;
        for (std::uint32_t channel = 0U; channel < channels_; ++channel) {
            const auto sample = input_interleaved[frame * channels_ + channel];
            ring_[channel][write_index_] = sample;
            output_interleaved[frame * channels_ + channel] = ring_[channel][read_index];
        }
        write_index_ = (write_index_ + 1U) % kRingSamples;
    }
    return true;
}

}  // namespace hibiki

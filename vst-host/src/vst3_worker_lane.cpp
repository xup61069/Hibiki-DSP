// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_worker_lane.hpp"

#include <cmath>

namespace hibiki {
namespace {

bool supported_channels(const std::uint32_t channels) noexcept {
    return channels == 2U || channels == 6U || channels == 8U;
}

}  // namespace

bool validate_vst3_worker_lane_config_v1(
    const Vst3WorkerLaneConfigV1& config) noexcept {
    return config.lane_token != 0U && supported_channels(config.channels) &&
           std::isfinite(config.sample_rate) && config.sample_rate >= 8000.0 &&
           config.sample_rate <= 384000.0 &&
           config.reported_latency_samples <= kLatencyGraphMaxSamplesV1 &&
           config.max_block_frames > 0U &&
           config.max_block_frames <= kVst3WorkerMaxFramesV1;
}

bool Vst3WorkerLaneSessionV1::prepare(
    Vst3SandboxProcess& sandbox,
    const Vst3WorkerLaneConfigV1& config) noexcept {
    detach();
    if (!validate_vst3_worker_lane_config_v1(config)) {
        state_ = Vst3WorkerLaneStateV1::Degraded;
        return false;
    }
    sandbox_ = &sandbox;
    config_ = config;
    next_block_start_ = 0U;
    has_processed_block_ = false;
    state_ = Vst3WorkerLaneStateV1::Prepared;
    return true;
}

Vst3WorkerExchangeResultV1 Vst3WorkerLaneSessionV1::handshake(
    const std::uint64_t request_id) {
    if (state_ != Vst3WorkerLaneStateV1::Prepared || sandbox_ == nullptr) {
        return Vst3WorkerExchangeResultV1::not_connected;
    }
    const auto result = sandbox_->handshake_worker(request_id);
    state_ = result == Vst3WorkerExchangeResultV1::ok
                 ? Vst3WorkerLaneStateV1::Ready
                 : Vst3WorkerLaneStateV1::Degraded;
    return result;
}

void Vst3WorkerLaneSessionV1::detach() noexcept {
    sandbox_ = nullptr;
    config_ = {};
    state_ = Vst3WorkerLaneStateV1::Detached;
    next_block_start_ = 0U;
    has_processed_block_ = false;
}

Vst3WorkerExchangeResultV1 Vst3WorkerLaneSessionV1::process_block(
    const std::uint64_t request_id,
    const std::uint64_t block_start,
    const std::uint32_t frames,
    const std::span<const float> input,
    const std::span<float> output) {
    if (state_ != Vst3WorkerLaneStateV1::Ready || sandbox_ == nullptr) {
        return Vst3WorkerExchangeResultV1::not_connected;
    }
    if (frames == 0U || frames > config_.max_block_frames ||
        (has_processed_block_ && block_start != next_block_start_)) {
        state_ = Vst3WorkerLaneStateV1::Degraded;
        return Vst3WorkerExchangeResultV1::invalid_argument;
    }
    const auto result = sandbox_->process_worker_block(
        request_id, config_.channels, frames, input, output,
        std::span<const Vst3WorkerParameterPointV1>());
    if (result != Vst3WorkerExchangeResultV1::ok) {
        state_ = Vst3WorkerLaneStateV1::Degraded;
        return result;
    }
    next_block_start_ = block_start + static_cast<std::uint64_t>(frames);
    if (next_block_start_ < block_start) {
        state_ = Vst3WorkerLaneStateV1::Degraded;
        return Vst3WorkerExchangeResultV1::invalid_argument;
    }
    has_processed_block_ = true;
    return Vst3WorkerExchangeResultV1::ok;
}

LatencyGraphLaneInputV1 Vst3WorkerLaneSessionV1::latency_lane_input() const noexcept {
    return LatencyGraphLaneInputV1{config_.lane_token,
                                   state_ == Vst3WorkerLaneStateV1::Ready,
                                   config_.channels,
                                   state_ == Vst3WorkerLaneStateV1::Ready
                                       ? config_.reported_latency_samples
                                       : 0U};
}

}  // namespace hibiki

#include "hibiki/audio_engine.hpp"

#include <cmath>

namespace hibiki {

bool AudioEngineModel::prepare_graph(const GraphConfigV1& graph,
                                     const std::uint64_t revision) noexcept {
    RtGraphSnapshotV1 candidate;
    if (!compile_rt_snapshot(graph, revision, candidate)) {
        state_ = EngineTransactionState::Degraded;
        has_pending_graph_ = false;
        return false;
    }
    pending_graph_ = candidate;
    has_pending_graph_ = true;
    state_ = EngineTransactionState::Prepared;
    return true;
}

bool AudioEngineModel::commit_graph() noexcept {
    if (!has_pending_graph_) {
        return false;
    }
    active_graph_ = pending_graph_;
    has_active_graph_ = true;
    has_pending_graph_ = false;
    state_ = EngineTransactionState::Ready;
    return true;
}

void AudioEngineModel::rollback_graph() noexcept {
    has_pending_graph_ = false;
    state_ = has_active_graph_ ? EngineTransactionState::Ready : EngineTransactionState::Degraded;
}

VolumeNotificationResult AudioEngineModel::apply_windows_volume(
    const VolumeNotificationV1& notification) noexcept {
    return apply_windows_notification(volume_, notification);
}

bool AudioEngineModel::process(const std::span<const RtLaneInputV1> inputs,
                               float* const output_interleaved,
                               const std::size_t frames) const noexcept {
    if (!has_active_graph_ || !process_graph(active_graph_, inputs, output_interleaved, frames)) {
        return false;
    }
    const auto samples = frames * static_cast<std::size_t>(active_graph_.output_channels);
    const float gain = volume_.mute || !std::isfinite(volume_.effective_db) ||
                               volume_.effective_db <= -144.0
                           ? 0.0F
                           : static_cast<float>(std::pow(10.0, volume_.effective_db / 20.0));
    for (std::size_t index = 0; index < samples; ++index) {
        output_interleaved[index] *= gain;
    }
    return true;
}

bool AudioEngineModel::bind_asio_transport(const std::wstring_view mapping_name,
                                           const std::uint32_t channels,
                                           const std::uint32_t sample_rate,
                                           const std::uint32_t frames_per_buffer) noexcept {
    return asio_transport_.bind(mapping_name, channels, sample_rate, frames_per_buffer);
}

void AudioEngineModel::unbind_asio_transport() noexcept { asio_transport_.unbind(); }

bool AudioEngineModel::asio_transport_bound() const noexcept { return asio_transport_.bound(); }

bool AudioEngineModel::process_asio_transport(
    const std::size_t lane_index,
    float* const transport_interleaved,
    const std::uint32_t transport_capacity_frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::size_t output_capacity_frames,
    AsioTransportBlockV1& block) noexcept {
    block = {};
    if (!has_active_graph_ || lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || transport_interleaved == nullptr ||
        output_interleaved == nullptr ||
        active_graph_.lanes[lane_index].input_channels == 0U) {
        return false;
    }
    if (!asio_transport_.pop(transport_interleaved, transport_capacity_frames, block) ||
        block.frames == 0U || block.frames > output_capacity_frames ||
        block.channels != active_graph_.lanes[lane_index].input_channels) {
        return false;
    }
    const auto previous = lane_inputs[lane_index];
    lane_inputs[lane_index] = RtLaneInputV1{transport_interleaved, block.channels};
    const bool processed = process(std::span<const RtLaneInputV1>(lane_inputs.data(), lane_inputs.size()),
                                   output_interleaved, block.frames);
    lane_inputs[lane_index] = previous;
    return processed;
}

}  // namespace hibiki

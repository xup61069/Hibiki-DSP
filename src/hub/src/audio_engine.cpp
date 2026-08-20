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
    const auto result = apply_windows_notification(volume_, notification);
    if (result == VolumeNotificationResult::Accepted) {
        // Release publishes a complete control-plane update; process() only
        // consumes these immutable scalar snapshots and never races volume_.
        const auto effective_q16 = db_to_q16_16(volume_.effective_db);
        const auto packed = (static_cast<std::uint64_t>(
                                 static_cast<std::uint32_t>(effective_q16)) << 32U) |
                            (volume_.mute ? 1ULL : 0ULL);
        rt_volume_word_.store(packed, std::memory_order_release);
    }
    return result;
}

bool AudioEngineModel::process(const std::span<const RtLaneInputV1> inputs,
                               float* const output_interleaved,
                               const std::size_t frames) const noexcept {
    if (!has_active_graph_ || !process_graph(active_graph_, inputs, output_interleaved, frames)) {
        return false;
    }
    const auto volume_word = rt_volume_word_.load(std::memory_order_acquire);
    const auto effective_q16 = static_cast<std::int32_t>(volume_word >> 32U);
    rt_volume_ramp_.observe_target(effective_q16, (volume_word & 1ULL) != 0ULL,
                                   sample_rate_.load(std::memory_order_relaxed));
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const auto gain = rt_volume_ramp_.next_gain();
        for (std::uint32_t channel = 0U; channel < active_graph_.output_channels; ++channel) {
            output_interleaved[frame * active_graph_.output_channels + channel] *= gain;
        }
    }
    return true;
}

bool AudioEngineModel::process_output_group(const std::string_view output_group,
                                            const std::span<const RtLaneInputV1> inputs,
                                            float* const output_interleaved,
                                            const std::size_t frames) const noexcept {
    if (!has_active_graph_ ||
        !process_graph_for_output_group(active_graph_, output_group, inputs,
                                        output_interleaved, frames)) {
        return false;
    }
    const auto volume_word = rt_volume_word_.load(std::memory_order_acquire);
    const auto effective_q16 = static_cast<std::int32_t>(volume_word >> 32U);
    rt_volume_ramp_.observe_target(effective_q16, (volume_word & 1ULL) != 0ULL,
                                   sample_rate_.load(std::memory_order_relaxed));
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const auto gain = rt_volume_ramp_.next_gain();
        for (std::uint32_t channel = 0U; channel < active_graph_.output_channels; ++channel) {
            output_interleaved[frame * active_graph_.output_channels + channel] *= gain;
        }
    }
    return true;
}

void AudioEngineModel::set_sample_rate(const std::uint32_t sample_rate) noexcept {
    if (sample_rate >= 8000U && sample_rate <= 192000U) {
        sample_rate_.store(sample_rate, std::memory_order_release);
    }
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
    return process_lane_block(lane_index, transport_interleaved, block.channels, block.frames,
                              lane_inputs, output_interleaved);
}

bool AudioEngineModel::process_lane_block(const std::size_t lane_index,
                                          const float* const input_interleaved,
                                          const std::uint32_t input_channels,
                                          const std::size_t frames,
                                          const std::span<RtLaneInputV1> lane_inputs,
                                          float* const output_interleaved) const noexcept {
    if (!has_active_graph_ || lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || input_interleaved == nullptr ||
        output_interleaved == nullptr || frames == 0U ||
        input_channels != active_graph_.lanes[lane_index].input_channels) {
        return false;
    }
    const auto previous = lane_inputs[lane_index];
    lane_inputs[lane_index] = RtLaneInputV1{input_interleaved, input_channels};
    const bool processed = process(std::span<const RtLaneInputV1>(lane_inputs.data(), lane_inputs.size()),
                                   output_interleaved, frames);
    lane_inputs[lane_index] = previous;
    return processed;
}

}  // namespace hibiki

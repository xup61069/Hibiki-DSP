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

}  // namespace hibiki

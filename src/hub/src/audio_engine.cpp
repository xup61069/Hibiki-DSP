#include "hibiki/audio_engine.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace hibiki {

bool AudioEngineModel::prepare_graph(const GraphConfigV1& graph,
                                     const std::uint64_t revision) noexcept {
    RtGraphSnapshotV1 candidate;
    if (!compile_rt_snapshot(graph, revision, candidate)) {
        state_ = EngineTransactionState::Degraded;
        has_pending_graph_ = false;
        pending_latency_bank_ = LaneLatencyBankV1{};
        return false;
    }
    std::array<LaneLatencyConfigV1, kMaxRtLanes> latency_configs{};
    for (std::size_t index = 0U; index < candidate.lane_count; ++index) {
        latency_configs[index] = LaneLatencyConfigV1{
            candidate.lanes[index].input_channels,
            candidate.lanes[index].compensation_delay_samples,
            candidate.lanes[index].enabled};
    }
    LaneLatencyBankV1 prepared_latency_bank;
    if (!prepared_latency_bank.prepare(
            std::span<const LaneLatencyConfigV1>(latency_configs.data(), candidate.lane_count))) {
        state_ = EngineTransactionState::Degraded;
        has_pending_graph_ = false;
        pending_latency_bank_ = LaneLatencyBankV1{};
        return false;
    }
    pending_graph_ = candidate;
    pending_latency_bank_ = std::move(prepared_latency_bank);
    has_pending_graph_ = true;
    state_ = EngineTransactionState::Prepared;
    return true;
}

bool AudioEngineModel::commit_graph() noexcept {
    if (!has_pending_graph_) {
        return false;
    }
    active_graph_ = pending_graph_;
    active_latency_bank_ = std::move(pending_latency_bank_);
    has_active_graph_ = true;
    has_pending_graph_ = false;
    state_ = EngineTransactionState::Ready;
    return true;
}

void AudioEngineModel::rollback_graph() noexcept {
    has_pending_graph_ = false;
    pending_latency_bank_ = LaneLatencyBankV1{};
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
    if (!has_active_graph_ ||
        !process_graph(active_graph_, inputs, output_interleaved, frames,
                       &active_latency_bank_)) {
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
    if (!active_graph_.strict_direct) {
        (void)rt_true_peak_limiter_.limit_in_place(
            output_interleaved, frames, active_graph_.output_channels, -1.0);
    }
    return true;
}

bool AudioEngineModel::process_output_group(const std::string_view output_group,
                                            const std::span<const RtLaneInputV1> inputs,
                                            float* const output_interleaved,
                                            const std::size_t frames) const noexcept {
    if (!has_active_graph_ ||
        !process_graph_for_output_group(active_graph_, output_group, inputs,
                                        output_interleaved, frames, &active_latency_bank_)) {
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
    if (!active_graph_.strict_direct) {
        (void)rt_true_peak_limiter_.limit_in_place(
            output_interleaved, frames, active_graph_.output_channels, -1.0);
    }
    return true;
}

bool AudioEngineModel::prepare_output_fanout(const OutputFanoutPlanV1& plan,
                                             const double source_step) noexcept {
    return output_fanout_.prepare(plan, source_step);
}

bool AudioEngineModel::observe_output_fanout_clock(const std::size_t sink_index,
                                                   const double source_frames,
                                                   const double sink_frames,
                                                   const double elapsed_seconds) noexcept {
    return output_fanout_.observe_clock(sink_index, source_frames, sink_frames,
                                        elapsed_seconds);
}

bool AudioEngineModel::process_output_group_fanout(
    const std::string_view output_group,
    const std::span<const RtLaneInputV1> inputs,
    float* const graph_output_interleaved,
    const std::size_t frames,
    const std::span<float* const> outputs,
    const std::span<const std::size_t> output_capacities,
    const std::span<std::size_t> output_frames) const noexcept {
    const auto fanout = output_fanout_.snapshot();
    if (!fanout.prepared || !has_active_graph_ ||
        fanout.output_channels != active_graph_.output_channels ||
        graph_output_interleaved == nullptr || frames == 0U ||
        !process_output_group(output_group, inputs, graph_output_interleaved, frames)) {
        return false;
    }
    return output_fanout_.process(graph_output_interleaved, frames, outputs,
                                  output_capacities, output_frames);
}

OutputFanoutRuntimeSnapshotV1 AudioEngineModel::output_fanout_snapshot() const noexcept {
    return output_fanout_.snapshot();
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

bool AudioEngineModel::process_asio_transport_to_wasapi(
    const std::size_t lane_index,
    float* const transport_interleaved,
    const std::uint32_t transport_capacity_frames,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved,
    const std::size_t output_capacity_frames,
    AsioTransportBlockV1& block) noexcept {
    if (!process_asio_transport(lane_index, transport_interleaved, transport_capacity_frames,
                                lane_inputs, output_interleaved, output_capacity_frames, block)) {
        return false;
    }
    if (static_cast<std::uint64_t>(block.frames) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        active_graph_.output_channels == 0U || active_graph_.output_channels > 8U) {
        return false;
    }
    return wasapi_handoff_.process(output_interleaved, block.frames,
                                   active_graph_.output_channels);
}

bool AudioEngineModel::process_driver_stream_packet(
    const std::size_t lane_index,
    const std::string_view expected_endpoint_guid,
    const std::span<const std::uint8_t> packet,
    const std::span<float> packet_sample_storage,
    const std::span<RtLaneInputV1> lane_inputs,
    float* const output_interleaved) const noexcept {
    if (!has_active_graph_ || expected_endpoint_guid.empty() || expected_endpoint_guid.size() >=
                                      HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1 ||
        lane_index >= active_graph_.lane_count ||
        lane_inputs.size() < active_graph_.lane_count || output_interleaved == nullptr ||
        active_graph_.lanes[lane_index].input_channels == 0U) {
        return false;
    }
    DriverStreamLaneBlockV1 block{};
    if (!decode_driver_stream_packet_v1(packet, packet_sample_storage, block) ||
        block.packet_type != HIBIKI_DRIVER_STREAM_RENDER_V1 ||
        std::string_view(block.endpoint_guid.data()) != expected_endpoint_guid ||
        block.sample_rate != sample_rate_.load(std::memory_order_acquire) ||
        block.channels != active_graph_.lanes[lane_index].input_channels ||
        block.interleaved == nullptr) {
        return false;
    }
    if (block.frames == 0U) return false;
    return process_lane_block(lane_index, block.interleaved, block.channels, block.frames,
                              lane_inputs, output_interleaved);
}

bool AudioEngineModel::start_wasapi_output(const WasapiOutputConfigV1& config,
                                           const std::uint32_t block_frames) noexcept {
    return wasapi_handoff_.start_initial(config, block_frames);
}

bool AudioEngineModel::begin_wasapi_output_handoff(const WasapiOutputConfigV1& candidate,
                                                   const std::uint32_t block_frames,
                                                   const std::uint32_t fade_ms) noexcept {
    return wasapi_handoff_.begin(candidate, block_frames, fade_ms);
}

bool AudioEngineModel::prepare_wasapi_output_handoff() noexcept {
    return wasapi_handoff_.prepare();
}

bool AudioEngineModel::commit_wasapi_output_handoff() noexcept {
    return wasapi_handoff_.commit();
}

void AudioEngineModel::rollback_wasapi_output_handoff() noexcept {
    wasapi_handoff_.rollback();
}

void AudioEngineModel::stop_wasapi_output() noexcept { wasapi_handoff_.stop(); }

WasapiSinkHandoffSnapshotV1 AudioEngineModel::wasapi_output_snapshot() const noexcept {
    return wasapi_handoff_.snapshot();
}

bool AudioEngineModel::process_output_group_to_wasapi(
    const std::string_view output_group,
    const std::span<const RtLaneInputV1> inputs,
    float* const graph_output_interleaved,
    const std::size_t frames) noexcept {
    if (frames == 0U || frames > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        graph_output_interleaved == nullptr || active_graph_.output_channels == 0U ||
        active_graph_.output_channels > 8U ||
        !process_output_group(output_group, inputs, graph_output_interleaved, frames)) {
        return false;
    }
    return wasapi_handoff_.process(graph_output_interleaved,
                                   static_cast<std::uint32_t>(frames),
                                   active_graph_.output_channels);
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

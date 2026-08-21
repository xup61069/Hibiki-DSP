#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_graph.hpp"
#include "hibiki/asio_transport_consumer.hpp"
#include "hibiki/driver_stream_bridge.hpp"
#include "hibiki/output_fanout.hpp"
#include "hibiki/volume_state.hpp"
#include "hibiki/true_peak_limiter.hpp"
#include "hibiki/windows_wasapi_handoff.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <span>
#include <string_view>

namespace hibiki {

enum class EngineTransactionState : std::uint8_t {
    Ready,
    Prepared,
    Degraded,
};

class AudioEngineModel final {
public:
    [[nodiscard]] bool prepare_graph(const GraphConfigV1& graph, std::uint64_t revision) noexcept;
    [[nodiscard]] bool commit_graph() noexcept;
    void rollback_graph() noexcept;
    [[nodiscard]] VolumeNotificationResult apply_windows_volume(
        const VolumeNotificationV1& notification) noexcept;
    [[nodiscard]] bool process(std::span<const RtLaneInputV1> inputs,
                               float* output_interleaved,
                               std::size_t frames) const noexcept;
    [[nodiscard]] bool process_output_group(std::string_view output_group,
                                             std::span<const RtLaneInputV1> inputs,
                                             float* output_interleaved,
                                             std::size_t frames) const noexcept;
    [[nodiscard]] bool prepare_output_fanout(const OutputFanoutPlanV1& plan,
                                              double source_step = 1.0) noexcept;
    [[nodiscard]] bool observe_output_fanout_clock(std::size_t sink_index,
                                                   double source_frames,
                                                   double sink_frames,
                                                   double elapsed_seconds) noexcept;
    [[nodiscard]] bool process_output_group_fanout(
        std::string_view output_group,
        std::span<const RtLaneInputV1> inputs,
        float* graph_output_interleaved,
        std::size_t frames,
        std::span<float* const> outputs,
        std::span<const std::size_t> output_capacities,
        std::span<std::size_t> output_frames) const noexcept;
    [[nodiscard]] OutputFanoutRuntimeSnapshotV1 output_fanout_snapshot() const noexcept;
    void set_sample_rate(std::uint32_t sample_rate) noexcept;
    [[nodiscard]] bool bind_asio_transport(std::wstring_view mapping_name,
                                            std::uint32_t channels,
                                            std::uint32_t sample_rate,
                                            std::uint32_t frames_per_buffer) noexcept;
    void unbind_asio_transport() noexcept;
    [[nodiscard]] bool asio_transport_bound() const noexcept;
    [[nodiscard]] bool process_asio_transport(
        std::size_t lane_index,
        float* transport_interleaved,
        std::uint32_t transport_capacity_frames,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved,
        std::size_t output_capacity_frames,
        AsioTransportBlockV1& block) noexcept;
    [[nodiscard]] bool process_asio_transport_to_wasapi(
        std::size_t lane_index,
        float* transport_interleaved,
        std::uint32_t transport_capacity_frames,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved,
        std::size_t output_capacity_frames,
        AsioTransportBlockV1& block) noexcept;
    [[nodiscard]] bool process_driver_stream_packet(
        std::size_t lane_index,
        std::string_view expected_endpoint_guid,
        std::span<const std::uint8_t> packet,
        std::span<float> packet_sample_storage,
        std::span<RtLaneInputV1> lane_inputs,
        float* output_interleaved) const noexcept;
    // Windows/WASAPI sink boundary. The graph remains the only producer of
    // audio blocks; handoff owns the two bounded sink workers and never
    // restarts this engine during a device switch.
    [[nodiscard]] bool start_wasapi_output(const WasapiOutputConfigV1& config,
                                           std::uint32_t block_frames = 128U) noexcept;
    [[nodiscard]] bool begin_wasapi_output_handoff(
        const WasapiOutputConfigV1& candidate,
        std::uint32_t block_frames = 128U,
        std::uint32_t fade_ms = 30U) noexcept;
    [[nodiscard]] bool prepare_wasapi_output_handoff() noexcept;
    [[nodiscard]] bool commit_wasapi_output_handoff() noexcept;
    void rollback_wasapi_output_handoff() noexcept;
    void stop_wasapi_output() noexcept;
    [[nodiscard]] WasapiSinkHandoffSnapshotV1 wasapi_output_snapshot() const noexcept;
    [[nodiscard]] bool process_output_group_to_wasapi(
        std::string_view output_group,
        std::span<const RtLaneInputV1> inputs,
        float* graph_output_interleaved,
        std::size_t frames) noexcept;
    [[nodiscard]] bool process_lane_block(std::size_t lane_index,
                                          const float* input_interleaved,
                                          std::uint32_t input_channels,
                                          std::size_t frames,
                                          std::span<RtLaneInputV1> lane_inputs,
                                          float* output_interleaved) const noexcept;
    [[nodiscard]] EngineTransactionState transaction_state() const noexcept { return state_; }
    [[nodiscard]] const RtGraphSnapshotV1& active_graph() const noexcept { return active_graph_; }
    // Control-plane snapshot.  The RT process path reads the two atomics
    // below instead of touching this mutable control-plane object.
    [[nodiscard]] OutputGroupVolumeStateV1 volume() const noexcept { return volume_; }

private:
    RtGraphSnapshotV1 active_graph_{};
    RtGraphSnapshotV1 pending_graph_{};
    mutable LaneLatencyBankV1 active_latency_bank_{};
    LaneLatencyBankV1 pending_latency_bank_{};
    OutputGroupVolumeStateV1 volume_{};
    // Upper 32 bits: signed Q16.16 effective dB; bit 0: mute.  One atomic
    // word keeps dB and mute coherent for a block boundary.
    std::atomic<std::uint64_t> rt_volume_word_{
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(-60 * 65536)) << 32U};
    std::atomic<std::uint32_t> sample_rate_{48000U};
    mutable VolumeRampProcessorV1 rt_volume_ramp_{};
    mutable TruePeakLimiterV1 rt_true_peak_limiter_{};
    EngineTransactionState state_{EngineTransactionState::Ready};
    bool has_active_graph_{false};
    bool has_pending_graph_{false};
    AsioTransportConsumerV1 asio_transport_{};
    mutable OutputFanoutRuntimeV1 output_fanout_{};
    WindowsWasapiSinkHandoffV1 wasapi_handoff_{};
};

}  // namespace hibiki

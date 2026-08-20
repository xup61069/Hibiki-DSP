#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_graph.hpp"
#include "hibiki/asio_transport_consumer.hpp"
#include "hibiki/volume_state.hpp"

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
    OutputGroupVolumeStateV1 volume_{};
    // Upper 32 bits: signed Q16.16 effective dB; bit 0: mute.  One atomic
    // word keeps dB and mute coherent for a block boundary.
    std::atomic<std::uint64_t> rt_volume_word_{
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(-60 * 65536)) << 32U};
    EngineTransactionState state_{EngineTransactionState::Ready};
    bool has_active_graph_{false};
    bool has_pending_graph_{false};
    AsioTransportConsumerV1 asio_transport_{};
};

}  // namespace hibiki

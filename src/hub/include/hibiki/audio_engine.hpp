#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/scene_graph.hpp"
#include "hibiki/volume_state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

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
    [[nodiscard]] EngineTransactionState transaction_state() const noexcept { return state_; }
    [[nodiscard]] const RtGraphSnapshotV1& active_graph() const noexcept { return active_graph_; }
    [[nodiscard]] const OutputGroupVolumeStateV1& volume() const noexcept { return volume_; }

private:
    RtGraphSnapshotV1 active_graph_{};
    RtGraphSnapshotV1 pending_graph_{};
    OutputGroupVolumeStateV1 volume_{};
    EngineTransactionState state_{EngineTransactionState::Ready};
    bool has_active_graph_{false};
    bool has_pending_graph_{false};
};

}  // namespace hibiki

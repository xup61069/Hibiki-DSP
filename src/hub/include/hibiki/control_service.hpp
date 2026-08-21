#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hibiki {

using ControlCommandSinkV1 = bool (*)(const ControlCommandV1& command,
                                      void* context) noexcept;
using DeviceCatalogSnapshotReplyV1 = bool (*)(IpcFrameV1& response,
                                              void* context) noexcept;

struct ControlPlaneHandlerContextV1 {
    ControlCommandSinkV1 sink{nullptr};
    void* sink_context{nullptr};
    DeviceCatalogSnapshotReplyV1 snapshot_reply{nullptr};
    void* snapshot_context{nullptr};
};

// Single-producer (pipe worker), single-consumer (control worker) queue. The
// command is copied into a fixed slot; no heap allocation, mutex or wait is
// permitted on either side. The consumer owns all AudioEngine mutations.
class ControlCommandQueueV1 final {
public:
    static constexpr std::size_t kCapacity = 64U;

    [[nodiscard]] bool try_push(const ControlCommandV1& command) noexcept;
    [[nodiscard]] bool try_pop(ControlCommandV1& command) noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    std::array<ControlCommandV1, kCapacity> slots_{};
    std::atomic<std::uint64_t> head_{0U};
    std::atomic<std::uint64_t> tail_{0U};
    std::atomic<std::uint64_t> dropped_{0U};
};

// Convenience adapter for ControlPlaneHandlerContextV1::sink_context.
[[nodiscard]] bool enqueue_control_command_v1(const ControlCommandV1& command,
                                              void* context) noexcept;

// Adapter suitable for IpcNamedPipeServerV1. It validates the typed command
// first, then delegates to a control-plane queue/sink supplied by the host.
// The sink must enqueue or otherwise hand off work; it must not run DSP on
// this pipe thread and must not throw.
[[nodiscard]] bool handle_control_frame_v1(const IpcFrameV1& request,
                                           IpcFrameV1& response,
                                           void* context) noexcept;

}  // namespace hibiki

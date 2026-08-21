#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"
#include "hibiki/control_status.hpp"
#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/ipc_pipe.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hibiki {

using ControlCommandSinkV1 = bool (*)(const ControlCommandV1& command,
                                      void* context) noexcept;
using DeviceCatalogSnapshotReplyV1 = bool (*)(IpcFrameV1& response,
                                              void* context) noexcept;
using ControlStatusSnapshotReplyV1 = bool (*)(IpcFrameV1& response,
                                              void* context) noexcept;

struct ControlPlaneHandlerContextV1 {
    ControlCommandSinkV1 sink{nullptr};
    void* sink_context{nullptr};
    DeviceCatalogSnapshotReplyV1 snapshot_reply{nullptr};
    void* snapshot_context{nullptr};
    ControlStatusSnapshotReplyV1 status_reply{nullptr};
    void* status_context{nullptr};
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

// Owns the control-plane pipe lifetime and the queue/provider context passed
// to the pipe worker. This is deliberately not an audio engine: the engine
// worker drains command_queue() on its own control thread, while the pipe only
// validates and hands off commands. A physical-device service may provide its
// snapshot_store() pointer; a missing store intentionally makes catalog
// requests return Error instead of an empty success snapshot.
class ControlPlaneHostV1 final {
public:
    ControlPlaneHostV1() noexcept = default;
    ~ControlPlaneHostV1() { stop(); }

    ControlPlaneHostV1(const ControlPlaneHostV1&) = delete;
    ControlPlaneHostV1& operator=(const ControlPlaneHostV1&) = delete;

    [[nodiscard]] bool start(
        const IpcNamedPipeConfigV1& config,
        ControlCommandSinkV1 sink,
        void* sink_context,
        DeviceCatalogSnapshotStoreV1* snapshot_store = nullptr,
        ControlStatusSnapshotStoreV1* status_store = nullptr) noexcept;

    [[nodiscard]] bool start_with_queue(
        const IpcNamedPipeConfigV1& config,
        DeviceCatalogSnapshotStoreV1* snapshot_store = nullptr,
        ControlStatusSnapshotStoreV1* status_store = nullptr) noexcept;

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return pipe_.running(); }
    [[nodiscard]] bool client_connected() const noexcept { return pipe_.client_connected(); }
    [[nodiscard]] ControlCommandQueueV1& command_queue() noexcept { return queue_; }

private:
    IpcNamedPipeServerV1 pipe_{};
    ControlCommandQueueV1 queue_{};
    ControlPlaneHandlerContextV1 context_{};
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

#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/control_service.hpp"
#include "hibiki/device_catalog.hpp"
#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/windows_audio_session_route.hpp"
#include "hibiki/windows_device_watcher.hpp"
#include "hibiki/windows_volume_broker.hpp"

#include <mmdeviceapi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hibiki {

// Worker-owned COM boundary for endpoint metadata. All methods must run on
// the same COM-initialized worker thread that owns the enumerator; notification
// callbacks only signal that this refresh is needed.
class WindowsPhysicalDeviceCatalogWorker final {
public:
    WindowsPhysicalDeviceCatalogWorker() noexcept = default;
    ~WindowsPhysicalDeviceCatalogWorker();

    WindowsPhysicalDeviceCatalogWorker(const WindowsPhysicalDeviceCatalogWorker&) = delete;
    WindowsPhysicalDeviceCatalogWorker& operator=(const WindowsPhysicalDeviceCatalogWorker&) = delete;

    [[nodiscard]] HRESULT bind(IMMDeviceEnumerator* enumerator) noexcept;
    void unbind() noexcept;

    // Refreshes the caller-owned catalog only after a complete enumeration.
    // A failed refresh leaves the previous catalog and sequence untouched.
    [[nodiscard]] HRESULT refresh(PhysicalDeviceCatalogV1& catalog,
                                  std::uint64_t& catalog_sequence) noexcept;

    // Same transaction, additionally producing the bounded UI snapshot. The
    // catalog is committed only if both enumeration and encoding succeed.
    [[nodiscard]] HRESULT refresh_snapshot(
        PhysicalDeviceCatalogV1& catalog,
        std::uint64_t& catalog_sequence,
        std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
        std::size_t& payload_bytes) noexcept;

private:
    [[nodiscard]] HRESULT enumerate_candidate(PhysicalDeviceCatalogV1& candidate,
                                               std::uint64_t sequence) const noexcept;

    IMMDeviceEnumerator* enumerator_{nullptr};
    DeviceCatalogSnapshotPublisherV1 publisher_{};
};

// Joins the allocation-free notification callback to the worker-owned
// enumerator. poll_and_refresh() is deliberately worker-driven: a callback
// only makes the next poll observable and never performs COM enumeration.
class WindowsPhysicalDeviceCatalogCoordinator final {
public:
    WindowsPhysicalDeviceCatalogCoordinator() noexcept = default;
    ~WindowsPhysicalDeviceCatalogCoordinator();

    WindowsPhysicalDeviceCatalogCoordinator(const WindowsPhysicalDeviceCatalogCoordinator&) = delete;
    WindowsPhysicalDeviceCatalogCoordinator& operator=(const WindowsPhysicalDeviceCatalogCoordinator&) = delete;

    [[nodiscard]] HRESULT bind(IMMDeviceEnumerator* enumerator) noexcept;
    void unbind() noexcept;
    [[nodiscard]] HRESULT refresh_now(
        PhysicalDeviceCatalogV1& catalog,
        std::uint64_t& catalog_sequence,
        std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
        std::size_t& payload_bytes) noexcept;
    [[nodiscard]] HRESULT poll_and_refresh(
        PhysicalDeviceCatalogV1& catalog,
        std::uint64_t& catalog_sequence,
        std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
        std::size_t& payload_bytes) noexcept;

private:
    WindowsDeviceWatcher* watcher_{nullptr};
    WindowsPhysicalDeviceCatalogWorker worker_{};
};

// Host-facing adapter that joins the worker-owned coordinator to the control
// service. Refreshes are transactional: the worker first builds a candidate
// catalog and wire frame, then the store publishes that frame before the
// service swaps its committed catalog. The snapshot reply callback is safe to
// call from a pipe worker and never performs COM work.
class WindowsPhysicalDeviceCatalogServiceV1 final {
public:
    WindowsPhysicalDeviceCatalogServiceV1() noexcept = default;
    ~WindowsPhysicalDeviceCatalogServiceV1() = default;

    WindowsPhysicalDeviceCatalogServiceV1(const WindowsPhysicalDeviceCatalogServiceV1&) = delete;
    WindowsPhysicalDeviceCatalogServiceV1& operator=(const WindowsPhysicalDeviceCatalogServiceV1&) = delete;

    [[nodiscard]] HRESULT bind(IMMDeviceEnumerator* enumerator) noexcept;
    void unbind() noexcept;
    [[nodiscard]] HRESULT refresh_now() noexcept;
    [[nodiscard]] HRESULT poll_and_refresh() noexcept;

    [[nodiscard]] bool has_snapshot() const noexcept { return snapshot_store_.has_snapshot(); }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return snapshot_store_.sequence(); }
    [[nodiscard]] DeviceCatalogSnapshotStoreV1* snapshot_store() noexcept {
        return &snapshot_store_;
    }
    [[nodiscard]] const PhysicalDeviceCatalogV1& catalog() const noexcept { return catalog_; }

private:
    [[nodiscard]] HRESULT refresh_impl(bool poll) noexcept;

    WindowsPhysicalDeviceCatalogCoordinator coordinator_{};
    PhysicalDeviceCatalogV1 catalog_{};
    std::uint64_t catalog_sequence_{0U};
    DeviceCatalogSnapshotStoreV1 snapshot_store_{};
};

// Minimal Windows control runtime composition. The caller owns the COM/
// engine threads: start() binds the worker-owned endpoint service and the
// local-only control host, refresh methods run on the caller's COM-initialized
// worker, and command_queue() is drained by EngineControlWorkerV1. No method
// here is an audio callback entry point.
class WindowsControlRuntimeV1 final {
public:
    WindowsControlRuntimeV1() noexcept = default;
    ~WindowsControlRuntimeV1() { stop(); }

    WindowsControlRuntimeV1(const WindowsControlRuntimeV1&) = delete;
    WindowsControlRuntimeV1& operator=(const WindowsControlRuntimeV1&) = delete;

    [[nodiscard]] bool start(IMMDeviceEnumerator* enumerator,
                             const IpcNamedPipeConfigV1& config) noexcept;
    void stop() noexcept;

    [[nodiscard]] HRESULT refresh_now() noexcept;
    [[nodiscard]] HRESULT poll_and_refresh() noexcept;
    [[nodiscard]] HRESULT refresh_default_volume(IMMDeviceEnumerator* enumerator) noexcept;
    [[nodiscard]] HRESULT refresh_default_volume_if_changed(
        IMMDeviceEnumerator* enumerator) noexcept;
    [[nodiscard]] HRESULT read_volume(OutputGroupVolumeStateV1& state) noexcept;
    [[nodiscard]] HRESULT write_volume(const OutputGroupVolumeStateV1& state,
                                       const GUID& event_context) noexcept;
    [[nodiscard]] bool poll_volume(WindowsVolumeNotificationSnapshotV1& snapshot) noexcept;
    [[nodiscard]] HRESULT write_session_volume(std::string_view session_instance_id,
                                               double requested_db,
                                               bool mute,
                                               const GUID& event_context) noexcept;
    [[nodiscard]] HRESULT read_session_volume(std::string_view session_instance_id,
                                              double& requested_db,
                                              bool& mute) noexcept;
    [[nodiscard]] bool running() const noexcept { return host_.running(); }
    [[nodiscard]] bool client_connected() const noexcept { return host_.client_connected(); }
    [[nodiscard]] bool volume_bound() const noexcept { return volume_broker_.is_bound(); }
    [[nodiscard]] ControlCommandQueueV1& command_queue() noexcept {
        return host_.command_queue();
    }
    [[nodiscard]] const PhysicalDeviceCatalogV1& catalog() const noexcept {
        return catalog_service_.catalog();
    }
    [[nodiscard]] DeviceCatalogSnapshotStoreV1* snapshot_store() noexcept {
        return catalog_service_.snapshot_store();
    }
    [[nodiscard]] ControlStatusSnapshotStoreV1* status_store() noexcept {
        return &status_store_;
    }
    // Publishes a caller-built control-plane status candidate. The caller must
    // supply route identities from its worker; this method never queries COM
    // or the RT graph.
    [[nodiscard]] bool publish_status_snapshot(
        const ControlStatusSnapshotV1& snapshot) noexcept;
    [[nodiscard]] std::uint64_t catalog_sequence() const noexcept {
        return catalog_service_.sequence();
    }

private:
    WindowsPhysicalDeviceCatalogServiceV1 catalog_service_{};
    ControlPlaneHostV1 host_{};
    WindowsVolumeBroker volume_broker_{};
    ControlStatusSnapshotStoreV1 status_store_{};
    ControlStatusSnapshotV1 status_snapshot_{};
    WindowsAudioSessionRouteCoordinatorV1 session_routes_{};

    [[nodiscard]] bool publish_status_volume(
        const OutputGroupVolumeStateV1& state) noexcept;
    [[nodiscard]] HRESULT refresh_default_session_routes(
        IMMDeviceEnumerator* enumerator) noexcept;
    [[nodiscard]] bool publish_session_route_status() noexcept;
};

}  // namespace hibiki

#endif  // defined(_WIN32)

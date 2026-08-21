#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#if defined(_WIN32)

#include "hibiki/device_catalog.hpp"
#include "hibiki/device_catalog_snapshot.hpp"
#include "hibiki/windows_device_watcher.hpp"

#include <mmdeviceapi.h>

#include <array>
#include <cstddef>
#include <cstdint>

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

}  // namespace hibiki

#endif  // defined(_WIN32)

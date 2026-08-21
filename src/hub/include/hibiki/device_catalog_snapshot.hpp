#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"
#include "hibiki/device_catalog.hpp"

#include <mutex>

namespace hibiki {

// Control-plane adapter used by a Windows worker after it has finished COM
// enumeration. It performs no COM calls and has no RT use: descriptors are
// copied into the Apache wire records and then encoded as one bounded frame.
class DeviceCatalogSnapshotPublisherV1 final {
public:
    [[nodiscard]] bool publish(
        const PhysicalDeviceCatalogV1& catalog,
        std::uint64_t catalog_sequence,
        std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
        std::size_t& payload_bytes) const noexcept;
};

// Control-plane cache for the most recently committed snapshot. Publication
// and replies are serialized here so a worker can replace the complete frame
// while a named-pipe request is being served; this cache is never touched by
// the RT graph. A missing snapshot fails closed instead of returning an empty
// success response.
class DeviceCatalogSnapshotStoreV1 final {
public:
    DeviceCatalogSnapshotStoreV1() noexcept = default;

    [[nodiscard]] bool publish(
        std::span<const std::uint8_t> payload,
        std::uint64_t catalog_sequence) noexcept;

    [[nodiscard]] bool reply(IpcFrameV1& response) const noexcept;

    [[nodiscard]] bool has_snapshot() const noexcept;
    [[nodiscard]] std::uint64_t sequence() const noexcept;

private:
    mutable std::mutex mutex_{};
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1> payload_{};
    std::size_t payload_bytes_{0U};
    std::uint64_t catalog_sequence_{0U};
};

// Adapter suitable for ControlPlaneHandlerContextV1::snapshot_reply. The
// context must point at a live DeviceCatalogSnapshotStoreV1 owned by the
// control host.
[[nodiscard]] bool device_catalog_snapshot_reply_v1(IpcFrameV1& response,
                                                    void* context) noexcept;

}  // namespace hibiki

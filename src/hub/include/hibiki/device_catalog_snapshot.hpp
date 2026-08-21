#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"
#include "hibiki/device_catalog.hpp"

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

}  // namespace hibiki

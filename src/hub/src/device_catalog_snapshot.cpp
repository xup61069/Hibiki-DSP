// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_catalog_snapshot.hpp"

#include <algorithm>

namespace hibiki {

bool DeviceCatalogSnapshotPublisherV1::publish(
    const PhysicalDeviceCatalogV1& catalog,
    const std::uint64_t catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) const noexcept {
    if (catalog.size() > kDeviceCatalogSnapshotCapacityV1) {
        payload.fill(0U);
        payload_bytes = 0U;
        return false;
    }
    std::array<DeviceCatalogSnapshotEntryV1, kDeviceCatalogSnapshotCapacityV1> entries{};
    const auto& source = catalog.entries();
    for (std::size_t index = 0U; index < catalog.size(); ++index) {
        const auto& descriptor = source[index];
        auto& target = entries[index];
        if (descriptor.endpoint_id.size() > kDeviceSwitchEndpointMaxBytesV1 ||
            descriptor.display_name.size() > target.display_name.size()) {
            payload.fill(0U);
            payload_bytes = 0U;
            return false;
        }
        target.endpoint_id_bytes = static_cast<std::uint16_t>(descriptor.endpoint_id.size());
        target.display_name_bytes = static_cast<std::uint16_t>(descriptor.display_name.size());
        std::copy(descriptor.endpoint_id.begin(), descriptor.endpoint_id.end(),
                  target.endpoint_id.begin());
        std::copy(descriptor.display_name.begin(), descriptor.display_name.end(),
                  target.display_name.begin());
        target.flow = static_cast<std::uint8_t>(descriptor.flow);
        target.availability = static_cast<std::uint8_t>(descriptor.availability);
        target.flags = descriptor.is_default ? 1U : 0U;
        target.channels = descriptor.channels;
        target.sample_rate = descriptor.sample_rate;
        target.buffer_frames = descriptor.buffer_frames;
        target.last_sequence = descriptor.last_sequence;
    }
    return encode_device_catalog_snapshot_v1(
        std::span<const DeviceCatalogSnapshotEntryV1>(entries.data(), catalog.size()),
        catalog_sequence, payload, payload_bytes);
}

}  // namespace hibiki

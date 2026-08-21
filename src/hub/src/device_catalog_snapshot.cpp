// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_catalog_snapshot.hpp"

#include <algorithm>
#include <exception>

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

bool DeviceCatalogSnapshotStoreV1::publish(
    const std::span<const std::uint8_t> payload,
    const std::uint64_t catalog_sequence) noexcept {
    if (payload.empty() || payload.size() > kDeviceCatalogSnapshotPayloadBytesV1 ||
        catalog_sequence == 0U) {
        return false;
    }
    DeviceCatalogSnapshotV1 decoded{};
    if (!decode_device_catalog_snapshot_v1(payload, decoded) ||
        decoded.catalog_sequence != catalog_sequence) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        std::copy(payload.begin(), payload.end(), payload_.begin());
        std::fill(payload_.begin() + static_cast<std::ptrdiff_t>(payload.size()),
                  payload_.end(), static_cast<std::uint8_t>(0U));
        payload_bytes_ = payload.size();
        catalog_sequence_ = catalog_sequence;
        return true;
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

bool DeviceCatalogSnapshotStoreV1::reply(IpcFrameV1& response) const noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (payload_bytes_ == 0U || catalog_sequence_ == 0U) {
            response = {};
            return false;
        }
        response = {};
        response.header.magic = kIpcMagicV1;
        response.header.version = kIpcVersionV1;
        response.header.type = IpcMessageType::DeviceCatalogSnapshot;
        response.header.payload_bytes = static_cast<std::uint32_t>(payload_bytes_);
        response.payload.assign(payload_.begin(),
                                payload_.begin() + static_cast<std::ptrdiff_t>(payload_bytes_));
        return true;
    } catch (const std::exception&) {
        response = {};
        return false;
    } catch (...) {
        response = {};
        return false;
    }
}

bool DeviceCatalogSnapshotStoreV1::has_snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return payload_bytes_ != 0U && catalog_sequence_ != 0U;
}

std::uint64_t DeviceCatalogSnapshotStoreV1::sequence() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return catalog_sequence_;
}

bool device_catalog_snapshot_reply_v1(IpcFrameV1& response,
                                      void* const context) noexcept {
    auto* store = static_cast<DeviceCatalogSnapshotStoreV1*>(context);
    return store != nullptr && store->reply(response);
}

}  // namespace hibiki

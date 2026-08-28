// SPDX-License-Identifier: Apache-2.0

#include "hibiki/control_status.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {
namespace {

void write_u16(std::uint8_t* bytes, const std::uint16_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

void write_u32(std::uint8_t* bytes, const std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void write_u64(std::uint8_t* bytes, const std::uint64_t value) noexcept {
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint64_t read_u64(const std::uint8_t* bytes) noexcept {
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

bool valid_volume(const OutputGroupVolumeStateV1& volume) noexcept {
    return volume.schema_version == 1U && std::isfinite(volume.requested_db) &&
           std::isfinite(volume.safety_ceiling_db) && std::isfinite(volume.effective_db) &&
           volume.requested_db >= -144.0 && volume.requested_db <= 12.0 &&
           volume.safety_ceiling_db >= -144.0 && volume.safety_ceiling_db <= 12.0 &&
           volume.effective_db >= -144.0 && volume.effective_db <= 12.0 &&
           volume.effective_db <= volume.requested_db + 0.05 &&
           volume.effective_db <= volume.safety_ceiling_db + 0.05 &&
           static_cast<std::uint8_t>(volume.origin) <=
               static_cast<std::uint8_t>(VolumeOrigin::Session) &&
           static_cast<std::uint8_t>(volume.actuator) <=
               static_cast<std::uint8_t>(ActuatorMode::StrictDirect);
}

bool valid_route(const ControlRouteHealthEntryV1& route) noexcept {
    if (route.id_bytes == 0U || route.id_bytes > 31U || route.name_bytes == 0U ||
        route.name_bytes > 63U || route.detail_bytes == 0U || route.detail_bytes > 119U ||
        route.state > ControlRouteHealthStateV1::Unavailable || (route.flags & ~1U) != 0U) {
        return false;
    }
    const std::string_view id(route.id.data(), route.id_bytes);
    const std::string_view name(route.name.data(), route.name_bytes);
    const std::string_view detail(route.detail.data(), route.detail_bytes);
    return is_printable_utf8_v1(id) && is_printable_utf8_v1(name) &&
           is_printable_utf8_v1(detail);
}

bool padded_zero(const std::uint8_t* bytes, const std::size_t used,
                 const std::size_t capacity) noexcept {
    for (std::size_t index = used; index < capacity; ++index) {
        if (bytes[index] != 0U) return false;
    }
    return true;
}

} // namespace

bool encode_control_status_snapshot_v1(
    const ControlStatusSnapshotV1& snapshot,
    std::array<std::uint8_t, kControlStatusSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    payload.fill(0U);
    payload_bytes = 0U;
    if (snapshot.sequence == 0U ||
        snapshot.route_count > kControlStatusSnapshotCapacityV1 ||
        !valid_volume(snapshot.volume)) {
        return false;
    }
    for (std::size_t index = 0U; index < snapshot.route_count; ++index) {
        const auto& route = snapshot.routes[index];
        if (!valid_route(route)) return false;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (route.id_bytes == snapshot.routes[previous].id_bytes &&
                std::equal(route.id.begin(), route.id.begin() + route.id_bytes,
                           snapshot.routes[previous].id.begin())) {
                return false;
            }
        }
    }
    write_u16(payload.data(), snapshot.route_count);
    write_u64(payload.data() + 4U, snapshot.sequence);
    write_u32(payload.data() + 12U,
              static_cast<std::uint32_t>(db_to_q16_16(snapshot.volume.requested_db)));
    write_u32(payload.data() + 16U,
              static_cast<std::uint32_t>(db_to_q16_16(snapshot.volume.safety_ceiling_db)));
    write_u32(payload.data() + 20U,
              static_cast<std::uint32_t>(db_to_q16_16(snapshot.volume.effective_db)));
    payload[24U] = snapshot.volume.mute ? 1U : 0U;
    payload[25U] = static_cast<std::uint8_t>(snapshot.volume.origin);
    payload[26U] = static_cast<std::uint8_t>(snapshot.volume.actuator);
    write_u64(payload.data() + 28U, snapshot.volume.generation);
    for (std::size_t index = 0U; index < snapshot.route_count; ++index) {
        const auto& route = snapshot.routes[index];
        auto* bytes = payload.data() + kControlStatusSnapshotHeaderBytesV1 +
                      (index * kControlStatusSnapshotEntryBytesV1);
        bytes[0U] = route.id_bytes;
        bytes[1U] = static_cast<std::uint8_t>(route.state);
        write_u16(bytes + 2U, route.flags);
        write_u16(bytes + 4U, route.name_bytes);
        write_u16(bytes + 6U, route.detail_bytes);
        std::copy_n(route.id.data(), route.id_bytes, bytes + 8U);
        std::copy_n(route.name.data(), route.name_bytes, bytes + 40U);
        std::copy_n(route.detail.data(), route.detail_bytes, bytes + 104U);
    }
    payload_bytes = kControlStatusSnapshotHeaderBytesV1 +
                    (static_cast<std::size_t>(snapshot.route_count) *
                     kControlStatusSnapshotEntryBytesV1);
    return true;
}

bool decode_control_status_snapshot_v1(
    const std::span<const std::uint8_t> payload,
    ControlStatusSnapshotV1& snapshot) noexcept {
    snapshot = {};
    if (payload.size() < kControlStatusSnapshotHeaderBytesV1 ||
        payload.size() > kControlStatusSnapshotPayloadBytesV1 || payload[2U] != 0U ||
        payload[3U] != 0U || payload[27U] != 0U || payload[36U] != 0U ||
        payload[37U] != 0U || payload[38U] != 0U || payload[39U] != 0U) {
        return false;
    }
    const auto route_count = static_cast<std::size_t>(read_u16(payload.data()));
    const auto expected_bytes = kControlStatusSnapshotHeaderBytesV1 +
                                (route_count * kControlStatusSnapshotEntryBytesV1);
    if (route_count > kControlStatusSnapshotCapacityV1 || payload.size() != expected_bytes ||
        read_u64(payload.data() + 4U) == 0U ||
        payload[24U] > 1U || payload[25U] > static_cast<std::uint8_t>(VolumeOrigin::Session) ||
        payload[26U] > static_cast<std::uint8_t>(ActuatorMode::StrictDirect)) {
        return false;
    }
    const auto requested_db = q16_16_to_db(static_cast<std::int32_t>(read_u32(payload.data() + 12U)));
    const auto ceiling_db = q16_16_to_db(static_cast<std::int32_t>(read_u32(payload.data() + 16U)));
    const auto effective_db = q16_16_to_db(static_cast<std::int32_t>(read_u32(payload.data() + 20U)));
    snapshot.sequence = read_u64(payload.data() + 4U);
    snapshot.volume.schema_version = 1U;
    snapshot.volume.requested_db = requested_db;
    snapshot.volume.safety_ceiling_db = ceiling_db;
    snapshot.volume.effective_db = effective_db;
    snapshot.volume.mute = payload[24U] != 0U;
    snapshot.volume.origin = static_cast<VolumeOrigin>(payload[25U]);
    snapshot.volume.actuator = static_cast<ActuatorMode>(payload[26U]);
    snapshot.volume.generation = read_u64(payload.data() + 28U);
    if (!valid_volume(snapshot.volume)) return false;

    for (std::size_t index = 0U; index < route_count; ++index) {
        const auto* bytes = payload.data() + kControlStatusSnapshotHeaderBytesV1 +
                            (index * kControlStatusSnapshotEntryBytesV1);
        const auto id_bytes = static_cast<std::size_t>(bytes[0U]);
        const auto name_bytes = static_cast<std::size_t>(read_u16(bytes + 4U));
        const auto detail_bytes = static_cast<std::size_t>(read_u16(bytes + 6U));
        if (id_bytes == 0U || id_bytes > 31U || name_bytes == 0U || name_bytes > 63U ||
            detail_bytes == 0U || detail_bytes > 119U || bytes[1U] > 4U ||
            (read_u16(bytes + 2U) & ~1U) != 0U ||
            !padded_zero(bytes + 8U, id_bytes, 32U) ||
            !padded_zero(bytes + 40U, name_bytes, 64U) ||
            !padded_zero(bytes + 104U, detail_bytes, 120U)) {
            return false;
        }
        auto& route = snapshot.routes[index];
        route.id_bytes = static_cast<std::uint8_t>(id_bytes);
        route.name_bytes = static_cast<std::uint16_t>(name_bytes);
        route.detail_bytes = static_cast<std::uint16_t>(detail_bytes);
        route.state = static_cast<ControlRouteHealthStateV1>(bytes[1U]);
        route.flags = read_u16(bytes + 2U);
        std::copy_n(bytes + 8U, id_bytes, route.id.data());
        std::copy_n(bytes + 40U, name_bytes, route.name.data());
        std::copy_n(bytes + 104U, detail_bytes, route.detail.data());
        if (!valid_route(route)) return false;
        for (std::size_t previous = 0U; previous < index; ++previous) {
            const auto& prior = snapshot.routes[previous];
            if (route.id_bytes == prior.id_bytes &&
                std::equal(route.id.begin(), route.id.begin() + route.id_bytes,
                           prior.id.begin())) {
                return false;
            }
        }
    }
    snapshot.route_count = static_cast<std::uint16_t>(route_count);
    return true;
}

bool ControlStatusSnapshotStoreV1::publish(const ControlStatusSnapshotV1& snapshot) noexcept {
    std::array<std::uint8_t, kControlStatusSnapshotPayloadBytesV1> candidate{};
    std::size_t candidate_bytes = 0U;
    if (!encode_control_status_snapshot_v1(snapshot, candidate, candidate_bytes)) return false;
    std::scoped_lock lock(mutex_);
    if (payload_bytes_ != 0U && snapshot.sequence <= sequence_) return false;
    payload_ = candidate;
    payload_bytes_ = candidate_bytes;
    sequence_ = snapshot.sequence;
    return true;
}

bool ControlStatusSnapshotStoreV1::reply(IpcFrameV1& response) const noexcept {
    std::scoped_lock lock(mutex_);
    if (payload_bytes_ == 0U) return false;
    response = {};
    response.header.type = IpcMessageType::ControlStatusSnapshot;
    response.header.payload_bytes = static_cast<std::uint32_t>(payload_bytes_);
    response.payload.assign(payload_.begin(), payload_.begin() +
                                           static_cast<std::ptrdiff_t>(payload_bytes_));
    return true;
}

bool ControlStatusSnapshotStoreV1::has_snapshot() const noexcept {
    std::scoped_lock lock(mutex_);
    return payload_bytes_ != 0U;
}

std::uint64_t ControlStatusSnapshotStoreV1::sequence() const noexcept {
    std::scoped_lock lock(mutex_);
    return sequence_;
}

bool control_status_snapshot_reply_v1(IpcFrameV1& response, void* const context) noexcept {
    auto* store = static_cast<ControlStatusSnapshotStoreV1*>(context);
    return store != nullptr && store->reply(response);
}

} // namespace hibiki

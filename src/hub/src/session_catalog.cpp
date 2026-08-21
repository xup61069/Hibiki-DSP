// SPDX-License-Identifier: Apache-2.0

#include "hibiki/session_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

namespace hibiki {
namespace {

void write_u16(std::uint8_t* const bytes, const std::uint16_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::uint8_t* const bytes, const std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

void write_u64(std::uint8_t* const bytes, const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
    }
}

std::uint16_t read_u16(const std::uint8_t* const bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* const bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t read_u64(const std::uint8_t* const bytes) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

bool zero_bytes(const std::uint8_t* const bytes, const std::size_t count) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        if (bytes[index] != 0U) return false;
    }
    return true;
}

bool valid_text(const char* const bytes, const std::size_t length,
                const std::size_t capacity) noexcept {
    if (length > capacity) return false;
    if (!is_printable_utf8_v1(std::string_view(bytes, length))) return false;
    return zero_bytes(reinterpret_cast<const std::uint8_t*>(bytes + length), capacity - length);
}

bool valid_entry(const SessionCatalogEntryV1& entry) noexcept {
    if (entry.handle == 0U || entry.active > 1U ||
        static_cast<std::uint8_t>(entry.route_state) >
            static_cast<std::uint8_t>(SessionCatalogRouteStateV1::Unavailable) ||
        (entry.flags & ~1U) != 0U || entry.mute > 1U ||
        !valid_text(entry.name.data(), entry.name_bytes, entry.name.size()) ||
        !valid_text(entry.app.data(), entry.app_bytes, entry.app.size()) ||
        !valid_text(entry.lane.data(), entry.lane_bytes, entry.lane.size()) ||
        !valid_text(entry.output.data(), entry.output_bytes, entry.output.size())) {
        return false;
    }
    if ((entry.flags & 1U) != 0U &&
        (entry.requested_db_q16_16 < (-144 * 65536) ||
         entry.requested_db_q16_16 > (12 * 65536))) {
        return false;
    }
    return true;
}

void encode_entry(const SessionCatalogEntryV1& entry, std::uint8_t* const bytes) noexcept {
    write_u64(bytes, entry.handle);
    bytes[8] = entry.active;
    bytes[9] = static_cast<std::uint8_t>(entry.route_state);
    write_u16(bytes + 10U, entry.flags);
    write_u32(bytes + 12U, static_cast<std::uint32_t>(entry.requested_db_q16_16));
    bytes[16] = entry.mute;
    bytes[17] = bytes[18] = bytes[19] = 0U;
    write_u16(bytes + 20U, entry.name_bytes);
    write_u16(bytes + 22U, entry.app_bytes);
    write_u16(bytes + 24U, entry.lane_bytes);
    write_u16(bytes + 26U, entry.output_bytes);
    std::memcpy(bytes + 28U, entry.name.data(), entry.name.size());
    std::memcpy(bytes + 92U, entry.app.data(), entry.app.size());
    std::memcpy(bytes + 156U, entry.lane.data(), entry.lane.size());
    std::memcpy(bytes + 204U, entry.output.data(), entry.output.size());
    std::fill(bytes + 252U, bytes + 256U, static_cast<std::uint8_t>(0U));
}

bool decode_entry(const std::uint8_t* const bytes, SessionCatalogEntryV1& entry) noexcept {
    entry = {};
    entry.handle = read_u64(bytes);
    entry.active = bytes[8];
    entry.route_state = static_cast<SessionCatalogRouteStateV1>(bytes[9]);
    entry.flags = read_u16(bytes + 10U);
    entry.requested_db_q16_16 = static_cast<std::int32_t>(read_u32(bytes + 12U));
    entry.mute = bytes[16];
    if (!zero_bytes(bytes + 17U, 3U)) return false;
    entry.name_bytes = read_u16(bytes + 20U);
    entry.app_bytes = read_u16(bytes + 22U);
    entry.lane_bytes = read_u16(bytes + 24U);
    entry.output_bytes = read_u16(bytes + 26U);
    std::memcpy(entry.name.data(), bytes + 28U, entry.name.size());
    std::memcpy(entry.app.data(), bytes + 92U, entry.app.size());
    std::memcpy(entry.lane.data(), bytes + 156U, entry.lane.size());
    std::memcpy(entry.output.data(), bytes + 204U, entry.output.size());
    return zero_bytes(bytes + 252U, 4U) && valid_entry(entry);
}

}  // namespace

bool encode_session_catalog_snapshot_v1(
    const SessionCatalogSnapshotV1& snapshot,
    std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    payload.fill(0U);
    payload_bytes = 0U;
    if (snapshot.sequence == 0U || snapshot.entry_count > kSessionCatalogSnapshotCapacityV1) {
        return false;
    }
    std::array<std::uint64_t, kSessionCatalogSnapshotCapacityV1> handles{};
    for (std::size_t index = 0U; index < snapshot.entry_count; ++index) {
        const auto& entry = snapshot.entries[index];
        if (!valid_entry(entry) ||
            std::find(handles.begin(), handles.begin() + static_cast<std::ptrdiff_t>(index),
                      entry.handle) != handles.begin() + static_cast<std::ptrdiff_t>(index)) {
            return false;
        }
        handles[index] = entry.handle;
        encode_entry(entry, payload.data() + kSessionCatalogSnapshotHeaderBytesV1 +
                                  (index * kSessionCatalogSnapshotEntryBytesV1));
    }
    write_u16(payload.data(), snapshot.entry_count);
    write_u16(payload.data() + 2U, 0U);
    write_u64(payload.data() + 4U, snapshot.sequence);
    write_u64(payload.data() + 12U, snapshot.generation);
    write_u32(payload.data() + 20U, 0U);
    payload_bytes = kSessionCatalogSnapshotHeaderBytesV1 +
                    (snapshot.entry_count * kSessionCatalogSnapshotEntryBytesV1);
    return true;
}

bool decode_session_catalog_snapshot_v1(const std::span<const std::uint8_t> payload,
                                        SessionCatalogSnapshotV1& snapshot) noexcept {
    snapshot = {};
    if (payload.size() < kSessionCatalogSnapshotHeaderBytesV1 ||
        payload.size() > kSessionCatalogSnapshotPayloadBytesV1 ||
        !zero_bytes(payload.data() + 2U, 2U) || !zero_bytes(payload.data() + 20U, 4U)) {
        return false;
    }
    const auto count = read_u16(payload.data());
    const auto expected = kSessionCatalogSnapshotHeaderBytesV1 +
                          (static_cast<std::size_t>(count) *
                           kSessionCatalogSnapshotEntryBytesV1);
    if (count > kSessionCatalogSnapshotCapacityV1 || payload.size() != expected ||
        read_u64(payload.data() + 4U) == 0U) {
        return false;
    }
    snapshot.entry_count = count;
    snapshot.sequence = read_u64(payload.data() + 4U);
    snapshot.generation = read_u64(payload.data() + 12U);
    for (std::size_t index = 0U; index < count; ++index) {
        if (!decode_entry(payload.data() + kSessionCatalogSnapshotHeaderBytesV1 +
                              (index * kSessionCatalogSnapshotEntryBytesV1),
                          snapshot.entries[index])) {
            snapshot = {};
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (snapshot.entries[prior].handle == snapshot.entries[index].handle) {
                snapshot = {};
                return false;
            }
        }
    }
    return true;
}

bool SessionCatalogSnapshotStoreV1::publish(
    const SessionCatalogSnapshotV1& snapshot) noexcept {
    std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1> candidate{};
    std::size_t candidate_bytes = 0U;
    if (!encode_session_catalog_snapshot_v1(snapshot, candidate, candidate_bytes)) return false;
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sequence_ != 0U && snapshot.sequence <= sequence_) return false;
        std::copy(candidate.begin(), candidate.begin() + static_cast<std::ptrdiff_t>(candidate_bytes),
                  payload_.begin());
        std::fill(payload_.begin() + static_cast<std::ptrdiff_t>(candidate_bytes), payload_.end(),
                  static_cast<std::uint8_t>(0U));
        payload_bytes_ = candidate_bytes;
        sequence_ = snapshot.sequence;
        return true;
    } catch (...) {
        return false;
    }
}

bool SessionCatalogSnapshotStoreV1::reply(IpcFrameV1& response) const noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (payload_bytes_ == 0U || sequence_ == 0U) {
            response = {};
            return false;
        }
        response = {};
        response.header.magic = kIpcMagicV1;
        response.header.version = kIpcVersionV1;
        response.header.type = IpcMessageType::SessionCatalogSnapshot;
        response.header.payload_bytes = static_cast<std::uint32_t>(payload_bytes_);
        response.payload.assign(payload_.begin(),
                                payload_.begin() + static_cast<std::ptrdiff_t>(payload_bytes_));
        return true;
    } catch (...) {
        response = {};
        return false;
    }
}

bool SessionCatalogSnapshotStoreV1::has_snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return payload_bytes_ != 0U && sequence_ != 0U;
}

std::uint64_t SessionCatalogSnapshotStoreV1::sequence() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

bool session_catalog_snapshot_reply_v1(IpcFrameV1& response, void* const context) noexcept {
    auto* store = static_cast<SessionCatalogSnapshotStoreV1*>(context);
    return store != nullptr && store->reply(response);
}

}  // namespace hibiki

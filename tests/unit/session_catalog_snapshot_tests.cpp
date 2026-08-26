// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ipc.hpp"
#include "hibiki/session_catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::decode_session_catalog_snapshot_v1;
using hibiki::encode_session_catalog_snapshot_v1;
using hibiki::IpcFrameV1;
using hibiki::kIpcMagicV1;
using hibiki::kIpcVersionV1;
using hibiki::kSessionCatalogSnapshotCapacityV1;
using hibiki::kSessionCatalogSnapshotEntryBytesV1;
using hibiki::kSessionCatalogSnapshotHeaderBytesV1;
using hibiki::kSessionCatalogSnapshotPayloadBytesV1;
using hibiki::session_catalog_snapshot_reply_v1;
using hibiki::SessionCatalogEntryV1;
using hibiki::SessionCatalogRouteStateV1;
using hibiki::SessionCatalogSnapshotStoreV1;
using IpcMessageType = hibiki::IpcMessageType;

void set_bounded_text(std::array<char, 64U>& field, std::uint16_t& length,
                      const std::string_view value)
{
    const auto bounded = value.size() > field.size() ? field.size() : value.size();
    std::copy_n(value.begin(), bounded, field.begin());
    length = static_cast<std::uint16_t>(bounded);
}

void set_bounded_text48(std::array<char, 48U>& field, std::uint16_t& length,
                        const std::string_view value)
{
    const auto bounded = value.size() > field.size() ? field.size() : value.size();
    std::copy_n(value.begin(), bounded, field.begin());
    length = static_cast<std::uint16_t>(bounded);
}

[[nodiscard]] SessionCatalogEntryV1 make_entry(const std::uint64_t handle,
                                               const bool active = true,
                                               const bool volume_available = true)
{
    auto entry = SessionCatalogEntryV1{};
    entry.handle = handle;
    entry.active = active ? 1U : 0U;
    entry.route_state = SessionCatalogRouteStateV1::Ready;
    entry.flags = volume_available ? 1U : 0U;
    entry.requested_db_q16_16 = -6 * 65536;
    entry.mute = 0U;
    set_bounded_text(entry.name, entry.name_bytes, "Spotify");
    set_bounded_text(entry.app, entry.app_bytes, "spotify.exe");
    set_bounded_text48(entry.lane, entry.lane_bytes, "lane-game");
    set_bounded_text48(entry.output, entry.output_bytes, "main");
    return entry;
}

[[nodiscard]] bool encode_two_entries(
    std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes, const std::uint64_t sequence,
    const std::uint64_t generation = 5U)
{
    auto snapshot = hibiki::SessionCatalogSnapshotV1{};
    snapshot.sequence = sequence;
    snapshot.generation = generation;
    snapshot.entry_count = 2U;
    snapshot.entries[0U] = make_entry(0x01020304U);
    snapshot.entries[1U] = make_entry(0x11121314U);
    snapshot.entries[1U].active = 0U;
    snapshot.entries[1U].route_state = SessionCatalogRouteStateV1::Pending;
    snapshot.entries[1U].flags = 0U;
    return encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes);
}

}  // namespace

int main()
{
    // ---- encode fails closed on zero sequence and capacity overflow ------
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{999U};

        auto zero_sequence = hibiki::SessionCatalogSnapshotV1{};
        zero_sequence.generation = 1U;
        zero_sequence.entry_count = 1U;
        zero_sequence.entries[0U] = make_entry(1U);
        CHECK(!encode_session_catalog_snapshot_v1(zero_sequence, payload, payload_bytes));
        CHECK(payload_bytes == 0U);

        auto overflow = hibiki::SessionCatalogSnapshotV1{};
        overflow.sequence = 9U;
        overflow.entry_count = kSessionCatalogSnapshotCapacityV1 + 1U;
        CHECK(!encode_session_catalog_snapshot_v1(overflow, payload, payload_bytes));
        CHECK(payload_bytes == 0U);

        auto duplicate = hibiki::SessionCatalogSnapshotV1{};
        duplicate.sequence = 9U;
        duplicate.entry_count = 2U;
        duplicate.entries[0U] = make_entry(77U);
        duplicate.entries[1U] = make_entry(77U);
        CHECK(!encode_session_catalog_snapshot_v1(duplicate, payload, payload_bytes));
        CHECK(payload_bytes == 0U);
    }

    // ---- encode rejects invalid entries ----------------------------------
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{999U};
        auto snapshot = hibiki::SessionCatalogSnapshotV1{};
        snapshot.sequence = 3U;

        auto zero_handle = make_entry(0U);
        snapshot.entry_count = 1U;
        snapshot.entries[0U] = zero_handle;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto bad_active = make_entry(5U);
        bad_active.active = 2U;
        snapshot.entries[0U] = bad_active;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto bad_flags = make_entry(5U);
        bad_flags.flags = 0x0002U;  // only bit 0 is defined
        snapshot.entries[0U] = bad_flags;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto bad_mute = make_entry(5U);
        bad_mute.mute = 2U;
        snapshot.entries[0U] = bad_mute;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto volume_out_of_range = make_entry(5U);
        volume_out_of_range.flags = 1U;  // bit 0 set: requested_db must be in range
        volume_out_of_range.requested_db_q16_16 = -200 * 65536;  // below -144 dB
        snapshot.entries[0U] = volume_out_of_range;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto above_ceiling = make_entry(5U);
        above_ceiling.requested_db_q16_16 = 13 * 65536;  // above +12 dB
        snapshot.entries[0U] = above_ceiling;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto control_character_name = make_entry(5U);
        control_character_name.name[0U] = '\t';
        snapshot.entries[0U] = control_character_name;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));

        auto bad_route_state = make_entry(5U);
        bad_route_state.route_state =
            static_cast<SessionCatalogRouteStateV1>(4U);  // beyond Unavailable
        snapshot.entries[0U] = bad_route_state;
        CHECK(!encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));
    }

    // ---- two-entry wire round-trip preserves header and entries ----------
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        payload.fill(0xa5U);
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 42U, 7U));
        CHECK(payload_bytes ==
              kSessionCatalogSnapshotHeaderBytesV1 +
                  (std::size_t{2U} * kSessionCatalogSnapshotEntryBytesV1));

        auto snapshot = hibiki::SessionCatalogSnapshotV1{};
        CHECK(decode_session_catalog_snapshot_v1({payload.data(), payload_bytes}, snapshot));
        CHECK(snapshot.sequence == 42U);
        CHECK(snapshot.generation == 7U);
        CHECK(snapshot.entry_count == 2U);

        const auto& first = snapshot.entries[0U];
        CHECK(first.handle == 0x01020304U);
        CHECK(first.active == 1U);
        CHECK(first.route_state == SessionCatalogRouteStateV1::Ready);
        CHECK((first.flags & 1U) != 0U);
        CHECK(first.requested_db_q16_16 == -6 * 65536);
        CHECK(first.mute == 0U);
        CHECK(std::string_view(first.name.data(), first.name_bytes) == "Spotify");
        CHECK(std::string_view(first.app.data(), first.app_bytes) == "spotify.exe");
        CHECK(std::string_view(first.lane.data(), first.lane_bytes) == "lane-game");
        CHECK(std::string_view(first.output.data(), first.output_bytes) == "main");

        const auto& second = snapshot.entries[1U];
        CHECK(second.handle == 0x11121314U);
        CHECK(second.active == 0U);
        CHECK(second.route_state == SessionCatalogRouteStateV1::Pending);
        CHECK(second.flags == 0U);
    }

    // ---- full-capacity snapshot encodes and round-trips ------------------
    {
        auto snapshot = hibiki::SessionCatalogSnapshotV1{};
        snapshot.sequence = 100U;
        snapshot.generation = 3U;
        snapshot.entry_count = kSessionCatalogSnapshotCapacityV1;
        for (std::size_t index = 0U; index < kSessionCatalogSnapshotCapacityV1; ++index) {
            snapshot.entries[index] =
                make_entry(static_cast<std::uint64_t>(index + 1U), index % 2U == 0U);
            if (index % 3U == 0U) {
                snapshot.entries[index].route_state = SessionCatalogRouteStateV1::Degraded;
            }
        }
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_session_catalog_snapshot_v1(snapshot, payload, payload_bytes));
        CHECK(payload_bytes == kSessionCatalogSnapshotPayloadBytesV1);
        auto decoded = hibiki::SessionCatalogSnapshotV1{};
        CHECK(decode_session_catalog_snapshot_v1({payload.data(), payload_bytes}, decoded));
        CHECK(decoded.entry_count == kSessionCatalogSnapshotCapacityV1);
        for (std::size_t index = 0U; index < kSessionCatalogSnapshotCapacityV1; ++index) {
            CHECK(decoded.entries[index].handle == static_cast<std::uint64_t>(index + 1U));
        }
    }

    // ---- decode rejects malformed payloads -------------------------------
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 50U));

        auto snapshot = hibiki::SessionCatalogSnapshotV1{};

        // Truncated below the fixed header.
        CHECK(!decode_session_catalog_snapshot_v1(
            {payload.data(), kSessionCatalogSnapshotHeaderBytesV1 - 1U}, snapshot));

        // Entry count larger than declared payload length.
        auto wrong_length = payload;
        wrong_length[0U] = 3U;
        CHECK(!decode_session_catalog_snapshot_v1(
            {wrong_length.data(), payload_bytes}, snapshot));

        // Reserved header bytes must be zero.
        auto reserved_set = payload;
        reserved_set[2U] = 1U;
        CHECK(!decode_session_catalog_snapshot_v1(
            {reserved_set.data(), payload_bytes}, snapshot));
        reserved_set = payload;
        reserved_set[20U] = 1U;
        CHECK(!decode_session_catalog_snapshot_v1(
            {reserved_set.data(), payload_bytes}, snapshot));

        // Zero sequence is rejected.
        auto zero_sequence = payload;
        zero_sequence[4U] = 0U;
        CHECK(!decode_session_catalog_snapshot_v1(
            {zero_sequence.data(), payload_bytes}, snapshot));

        // Non-zero entry padding is rejected.
        auto padded_entry = payload;
        padded_entry[kSessionCatalogSnapshotHeaderBytesV1 + 17U] = 1U;
        CHECK(!decode_session_catalog_snapshot_v1(
            {padded_entry.data(), payload_bytes}, snapshot));

        // Non-zero trailing padding after the last entry is rejected.
        auto trailing = payload;
        trailing[payload_bytes - 1U] = 1U;
        CHECK(!decode_session_catalog_snapshot_v1(
            {trailing.data(), payload_bytes}, snapshot));

        // Duplicate handles inside a decoded frame are rejected.
        auto duplicated = payload;
        const auto base2 = kSessionCatalogSnapshotHeaderBytesV1 +
                           kSessionCatalogSnapshotEntryBytesV1;
        for (std::size_t index = 0U; index < 8U; ++index) {
            duplicated[base2 + index] =
                duplicated[kSessionCatalogSnapshotHeaderBytesV1 + index];
        }
        CHECK(!decode_session_catalog_snapshot_v1(
            {duplicated.data(), payload_bytes}, snapshot));
    }

    // ---- store enforces strictly increasing sequences --------------------
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 42U));

        auto store = SessionCatalogSnapshotStoreV1{};
        CHECK(store.has_snapshot() == false);
        CHECK(store.sequence() == 0U);

        auto valid = hibiki::SessionCatalogSnapshotV1{};
        CHECK(decode_session_catalog_snapshot_v1({payload.data(), payload_bytes}, valid));
        CHECK(valid.sequence == 42U);

        auto zero_sequence = valid;
        zero_sequence.sequence = 0U;
        CHECK(!store.publish(zero_sequence));  // codec rejects zero sequence
        CHECK(store.has_snapshot() == false);

        CHECK(store.publish(valid));
        CHECK(store.has_snapshot());
        CHECK(store.sequence() == 42U);
        CHECK(!store.publish(valid));  // same sequence rejected

        auto newer = valid;
        newer.sequence = 41U;
        CHECK(!store.publish(newer));  // older sequence rejected
        CHECK(store.sequence() == 42U);

        newer.sequence = 43U;
        CHECK(store.publish(newer));
        CHECK(store.sequence() == 43U);
    }

    // ---- reply is fail-closed before publication and exact after it ------
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 46U));

        auto empty_store = SessionCatalogSnapshotStoreV1{};
        auto response = IpcFrameV1{};
        response.header.magic = kIpcMagicV1;
        response.header.payload_bytes = 123U;
        CHECK(!empty_store.reply(response));
        CHECK(response.payload.empty());
        CHECK(response.header.payload_bytes == 0U);

        auto store = SessionCatalogSnapshotStoreV1{};
        auto snapshot = hibiki::SessionCatalogSnapshotV1{};
        CHECK(decode_session_catalog_snapshot_v1({payload.data(), payload_bytes}, snapshot));
        snapshot.sequence = 46U;
        CHECK(store.publish(snapshot));
        response = IpcFrameV1{};
        CHECK(store.reply(response));
        CHECK(response.header.magic == kIpcMagicV1);
        CHECK(response.header.version == kIpcVersionV1);
        CHECK(response.header.type == IpcMessageType::SessionCatalogSnapshot);
        CHECK(response.header.payload_bytes == payload_bytes);
        CHECK(response.payload.size() == payload_bytes);
        CHECK(std::equal(payload.begin(),
                         payload.begin() + static_cast<std::ptrdiff_t>(payload_bytes),
                         response.payload.begin()));
    }

    // ---- callback adapter forwards only through a live store context -----
    {
        auto payload = std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>{};
        auto payload_bytes = std::size_t{0U};
        CHECK(encode_two_entries(payload, payload_bytes, 47U));

        auto response = IpcFrameV1{};
        CHECK(!session_catalog_snapshot_reply_v1(response, nullptr));
        auto empty_store = SessionCatalogSnapshotStoreV1{};
        CHECK(!session_catalog_snapshot_reply_v1(response, &empty_store));
        auto store = SessionCatalogSnapshotStoreV1{};
        auto snapshot = hibiki::SessionCatalogSnapshotV1{};
        CHECK(decode_session_catalog_snapshot_v1({payload.data(), payload_bytes}, snapshot));
        snapshot.sequence = 47U;
        CHECK(store.publish(snapshot));
        CHECK(session_catalog_snapshot_reply_v1(response, &store));
        CHECK(response.header.type == IpcMessageType::SessionCatalogSnapshot);
        CHECK(response.header.payload_bytes == payload_bytes);
    }

    std::fputs("session catalog snapshot tests passed\n", stdout);
    return 0;
}


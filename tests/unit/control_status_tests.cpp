// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_status.hpp"
#include "hibiki/ipc.hpp"
#include "hibiki/volume_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::ActuatorMode;
using hibiki::ControlRouteHealthEntryV1;
using hibiki::ControlRouteHealthStateV1;
using hibiki::ControlStatusSnapshotStoreV1;
using hibiki::ControlStatusSnapshotV1;
using hibiki::VolumeOrigin;

void fill_route(ControlRouteHealthEntryV1& route,
                const char* const id,
                const char* const name,
                const char* const detail,
                const ControlRouteHealthStateV1 state,
                const bool requires_action) {
    route = {};
    route.id_bytes = static_cast<std::uint8_t>(std::string_view{id}.size());
    route.name_bytes = static_cast<std::uint16_t>(std::string_view{name}.size());
    route.detail_bytes = static_cast<std::uint16_t>(std::string_view{detail}.size());
    route.state = state;
    route.flags = requires_action ? 1U : 0U;
    std::copy_n(id, route.id_bytes, route.id.begin());
    std::copy_n(name, route.name_bytes, route.name.begin());
    std::copy_n(detail, route.detail_bytes, route.detail.begin());
}

void fill_route_repeat_id(ControlRouteHealthEntryV1& route,
                          const std::size_t id_repeat,
                          const char id_char,
                          const char* const name,
                          const char* const detail,
                          const ControlRouteHealthStateV1 state,
                          const bool requires_action) {
    route = {};
    route.id_bytes = static_cast<std::uint8_t>(id_repeat);
    route.name_bytes = static_cast<std::uint16_t>(std::string_view{name}.size());
    route.detail_bytes = static_cast<std::uint16_t>(std::string_view{detail}.size());
    route.state = state;
    route.flags = requires_action ? 1U : 0U;
    std::fill_n(route.id.begin(), id_repeat, id_char);
    std::copy_n(name, route.name_bytes, route.name.begin());
    std::copy_n(detail, route.detail_bytes, route.detail.begin());
}

void fill_route_repeat_name(ControlRouteHealthEntryV1& route,
                            const char* const id,
                            const std::size_t name_repeat,
                            const char name_char,
                            const char* const detail,
                            const ControlRouteHealthStateV1 state,
                            const bool requires_action) {
    route = {};
    route.id_bytes = static_cast<std::uint8_t>(std::string_view{id}.size());
    route.name_bytes = static_cast<std::uint16_t>(name_repeat);
    route.detail_bytes = static_cast<std::uint16_t>(std::string_view{detail}.size());
    route.state = state;
    route.flags = requires_action ? 1U : 0U;
    std::copy_n(id, route.id_bytes, route.id.begin());
    std::fill_n(route.name.begin(), name_repeat, name_char);
    std::copy_n(detail, route.detail_bytes, route.detail.begin());
}

void fill_route_repeat_detail(ControlRouteHealthEntryV1& route,
                              const char* const id,
                              const char* const name,
                              const std::size_t detail_repeat,
                              const char detail_char,
                              const ControlRouteHealthStateV1 state,
                              const bool requires_action) {
    route = {};
    route.id_bytes = static_cast<std::uint8_t>(std::string_view{id}.size());
    route.name_bytes = static_cast<std::uint16_t>(std::string_view{name}.size());
    route.detail_bytes = static_cast<std::uint16_t>(detail_repeat);
    route.state = state;
    route.flags = requires_action ? 1U : 0U;
    std::copy_n(id, route.id_bytes, route.id.begin());
    std::copy_n(name, route.name_bytes, route.name.begin());
    std::fill_n(route.detail.begin(), detail_repeat, detail_char);
}

ControlStatusSnapshotV1 make_valid_snapshot(const std::uint16_t route_count) {
    ControlStatusSnapshotV1 snapshot{};
    snapshot.sequence = 7U;
    snapshot.volume.requested_db = -6.0;
    snapshot.volume.safety_ceiling_db = -12.0;
    snapshot.volume.effective_db = -12.0;
    snapshot.volume.generation = 4U;
    snapshot.volume.origin = VolumeOrigin::Safety;
    snapshot.volume.actuator = ActuatorMode::InternalDsp;
    snapshot.route_count = route_count;
    fill_route(snapshot.routes[0], "windows-session", "Windows Session",
               "waiting for active session", ControlRouteHealthStateV1::Pending, false);
    if (route_count > 1U) {
        fill_route(snapshot.routes[1], "browser-tab", "Chrome tab",
                   "requires extension click", ControlRouteHealthStateV1::Pending, true);
        fill_route(snapshot.routes[2], "engine-graph", "Engine graph",
                   "graph ready", ControlRouteHealthStateV1::Ready, false);
        fill_route(snapshot.routes[3], "output-sink", "Output sink",
                   "sink degraded", ControlRouteHealthStateV1::Degraded, true);
        fill_route(snapshot.routes[4], "asio-driver", "ASIO driver",
                   "driver bypassed", ControlRouteHealthStateV1::Bypassed, false);
        fill_route(snapshot.routes[5], "virtual-mic", "Virtual mic",
                   "mic unavailable", ControlRouteHealthStateV1::Unavailable, true);
        fill_route(snapshot.routes[6], "scene-catalog", "Scene catalog",
                   "catalog loaded", ControlRouteHealthStateV1::Ready, false);
        fill_route(snapshot.routes[7], "volume-link", "Volume link",
                   "link active", ControlRouteHealthStateV1::Ready, false);
    }
    return snapshot;
}

}  // namespace

int main() {
    // ---- round-trip: zero routes -------------------------------------------
    {
        const auto snapshot = make_valid_snapshot(0U);
        std::array<std::uint8_t, hibiki::kControlStatusSnapshotPayloadBytesV1> payload{};
        std::size_t bytes = 0U;
        CHECK(hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));
        CHECK(bytes == hibiki::kControlStatusSnapshotHeaderBytesV1);
        hibiki::ControlStatusSnapshotV1 decoded{};
        CHECK(hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(payload.data(), bytes), decoded));
        CHECK(decoded.route_count == 0U && decoded.sequence == 7U &&
              decoded.volume.generation == 4U);
    }

    // ---- round-trip: single route ------------------------------------------
    {
        const auto snapshot = make_valid_snapshot(1U);
        std::array<std::uint8_t, hibiki::kControlStatusSnapshotPayloadBytesV1> payload{};
        std::size_t bytes = 0U;
        CHECK(hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));
        CHECK(bytes == hibiki::kControlStatusSnapshotHeaderBytesV1 +
                            hibiki::kControlStatusSnapshotEntryBytesV1);
        hibiki::ControlStatusSnapshotV1 decoded{};
        CHECK(hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(payload.data(), bytes), decoded));
        CHECK(decoded.route_count == 1U && decoded.sequence == 7U);
        CHECK(decoded.volume.origin == VolumeOrigin::Safety &&
              decoded.volume.actuator == ActuatorMode::InternalDsp);
        CHECK(std::string_view(decoded.routes[0].id.data(), decoded.routes[0].id_bytes)
              == "windows-session");
        CHECK(std::string_view(decoded.routes[0].name.data(), decoded.routes[0].name_bytes)
              == "Windows Session");
        CHECK(decoded.routes[0].state == ControlRouteHealthStateV1::Pending);
        CHECK(decoded.routes[0].flags == 0U);
    }

    // ---- round-trip: capacity boundary (all 8 route slots) -------------------
    {
        auto snapshot = make_valid_snapshot(2U);
        snapshot.route_count = hibiki::kControlStatusSnapshotCapacityV1;
        snapshot.sequence = 99U;
        std::array<std::uint8_t, hibiki::kControlStatusSnapshotPayloadBytesV1> payload{};
        std::size_t bytes = 0U;
        CHECK(hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));
        CHECK(bytes == hibiki::kControlStatusSnapshotPayloadBytesV1);
        hibiki::ControlStatusSnapshotV1 decoded{};
        CHECK(hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(payload.data(), bytes), decoded));
        CHECK(decoded.route_count == 8U);
        CHECK(std::string_view(decoded.routes[7].id.data(), decoded.routes[7].id_bytes)
              == "volume-link");
    }

    // ---- volume validation: fail-closed paths -------------------------------
    {
        auto snapshot = make_valid_snapshot(0U);

        snapshot.volume.schema_version = 2U;
        std::array<std::uint8_t, hibiki::kControlStatusSnapshotPayloadBytesV1> payload{};
        std::size_t bytes = 0U;
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.volume.requested_db = -144.5;   // below floor
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.volume.requested_db = 12.5;     // above ceiling
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.volume.effective_db = std::nan("");
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        // Raise the ceiling so the only violated rule is effective > requested.
        snapshot = make_valid_snapshot(0U);
        snapshot.volume.safety_ceiling_db = 0.0;
        snapshot.volume.effective_db = snapshot.volume.requested_db + 0.5;
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.volume.origin = static_cast<VolumeOrigin>(9U);
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.volume.actuator = static_cast<ActuatorMode>(7U);
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        // Boundary values that must pass.
        snapshot = make_valid_snapshot(0U);
        snapshot.volume.requested_db = -144.0;
        snapshot.volume.effective_db = -144.0;
        CHECK(hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));
        snapshot = make_valid_snapshot(0U);
        snapshot.volume.safety_ceiling_db = 12.0;
        snapshot.volume.requested_db = 12.0;
        snapshot.volume.effective_db = 12.0;
        CHECK(hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));
    }

    // ---- route validation: fail-closed paths ---------------------------------
    {
        auto snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 1U;
        snapshot.routes[0] = {};
        snapshot.routes[0].id_bytes = 0U;  // empty id
        std::array<std::uint8_t, hibiki::kControlStatusSnapshotPayloadBytesV1> payload{};
        std::size_t bytes = 0U;
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 1U;
        fill_route_repeat_id(snapshot.routes[0], 32U, 'x', "name", "detail",
                             ControlRouteHealthStateV1::Ready, false);  // 32-byte id exceeds max 31
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 1U;
        fill_route_repeat_name(snapshot.routes[0], "id", 64U, 'n', "detail",
                               ControlRouteHealthStateV1::Ready, false);  // 64-byte name exceeds max 63
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 1U;
        fill_route_repeat_detail(snapshot.routes[0], "id", "name", 120U, 'd',
                                 ControlRouteHealthStateV1::Ready, false);  // 120-byte detail exceeds max 119
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 1U;
        fill_route(snapshot.routes[0], "id", "name", "detail",
                   static_cast<ControlRouteHealthStateV1>(5U), false);  // invalid state
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 1U;
        fill_route(snapshot.routes[0], "id", "name", "detail",
                   ControlRouteHealthStateV1::Ready, false);
        snapshot.routes[0].flags = 3U;  // unknown flag bits
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        // Duplicate route IDs fail closed.
        snapshot = make_valid_snapshot(0U);
        snapshot.route_count = 2U;
        fill_route(snapshot.routes[0], "dup", "first", "first detail",
                   ControlRouteHealthStateV1::Ready, false);
        fill_route(snapshot.routes[1], "dup", "second", "second detail",
                   ControlRouteHealthStateV1::Pending, false);
        CHECK(!hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));
    }

    // ---- decode rejection: truncated / oversized / reserved non-zero ----------
    {
        const auto snapshot = make_valid_snapshot(2U);
        std::array<std::uint8_t, hibiki::kControlStatusSnapshotPayloadBytesV1> payload{};
        std::size_t bytes = 0U;
        CHECK(hibiki::encode_control_status_snapshot_v1(snapshot, payload, bytes));

        hibiki::ControlStatusSnapshotV1 decoded{};

        // Below header size.
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(payload.data(),
                                          hibiki::kControlStatusSnapshotHeaderBytesV1 - 1U),
            decoded));

        // Truncated entry.
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(payload.data(), bytes - 1U), decoded));

        // Oversized (full capacity plus one extra byte).
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(payload.data(), bytes + 1U), decoded));

        // Reserved header bytes must stay zero.
        auto corrupted = payload;
        corrupted[2U] = 1U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));
        corrupted = payload;
        corrupted[27U] = 1U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));
        corrupted = payload;
        corrupted[36U] = 1U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));

        // mute must be 0 or 1.
        corrupted = payload;
        corrupted[24U] = 2U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));

        // origin/actuator enum bounds.
        corrupted = payload;
        corrupted[25U] = 6U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));
        corrupted = payload;
        corrupted[26U] = 4U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));

        // Route padding after the declared length must stay zero.
        corrupted = payload;
        // First entry starts at header 40; id field at entry +8; declared 15 bytes
        // ("windows-session"); poison the first padding byte inside the id field.
        constexpr std::size_t kFirstEntryBase = 40U;
        constexpr std::size_t kFirstIdLen = 15U;
        corrupted[kFirstEntryBase + 8U + kFirstIdLen] = 'X';
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));

        // Non-printable byte in route detail fails closed.
        corrupted = payload;
        // Second entry detail starts at entry +104; original text begins "requires...".
        constexpr std::size_t kSecondEntryBase =
            40U + hibiki::kControlStatusSnapshotEntryBytesV1;
        corrupted[kSecondEntryBase + 104U] = 0x01U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));

        // Unknown state value fails closed on decode too.
        corrupted = payload;
        corrupted[kFirstEntryBase + 1U] = 5U;
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));

        // Duplicate IDs fail closed on decode as well: give the second entry the
        // same declared id length and the same id content as the first entry.
        corrupted = payload;
        constexpr std::size_t kSecondIdOffset = kSecondEntryBase + 8U;
        constexpr std::size_t kFirstIdOffset = kFirstEntryBase + 8U;
        corrupted[kSecondEntryBase] =
            corrupted[kFirstEntryBase];  // match declared id_bytes
        for (std::size_t i = 0U; i < kFirstIdLen; ++i) {
            corrupted[kSecondIdOffset + i] = corrupted[kFirstIdOffset + i];
        }
        CHECK(!hibiki::decode_control_status_snapshot_v1(
            std::span<const std::uint8_t>(corrupted.data(), bytes), decoded));
    }

    // ---- store behavior -------------------------------------------------------
    {
        hibiki::ControlStatusSnapshotStoreV1 store;
        hibiki::IpcFrameV1 response;
        CHECK(!store.has_snapshot());
        CHECK(!store.reply(response));
        CHECK(store.sequence() == 0U);

        auto first = make_valid_snapshot(1U);
        first.sequence = 5U;
        CHECK(store.publish(first));
        CHECK(store.has_snapshot() && store.sequence() == 5U);
        CHECK(store.reply(response));
        CHECK(response.header.type == hibiki::IpcMessageType::ControlStatusSnapshot &&
              response.payload.size() ==
                  hibiki::kControlStatusSnapshotHeaderBytesV1 +
                      hibiki::kControlStatusSnapshotEntryBytesV1);

        // Stale sequence rejected.
        auto stale = make_valid_snapshot(1U);
        stale.sequence = 4U;
        CHECK(!store.publish(stale) && store.sequence() == 5U);

        // Same sequence rejected.
        auto same = make_valid_snapshot(1U);
        same.sequence = 5U;
        CHECK(!store.publish(same));

        // Fresh sequence accepted and replaces content.
        auto fresh = make_valid_snapshot(0U);
        fresh.sequence = 6U;
        CHECK(store.publish(fresh) && store.sequence() == 6U);
        CHECK(store.reply(response) && response.payload.size() ==
                                            hibiki::kControlStatusSnapshotHeaderBytesV1);

        // Null context adapter fails closed.
        CHECK(!hibiki::control_status_snapshot_reply_v1(response, nullptr));
    }

    return 0;
}

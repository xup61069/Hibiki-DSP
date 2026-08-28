#pragma once

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/control_payloads.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace hibiki {

constexpr std::size_t kSessionCatalogSnapshotHeaderBytesV1 = 24U;
constexpr std::size_t kSessionCatalogSnapshotEntryBytesV1 = 256U;
constexpr std::size_t kSessionCatalogSnapshotCapacityV1 = 32U;
constexpr std::size_t kSessionCatalogSnapshotPayloadBytesV1 =
    kSessionCatalogSnapshotHeaderBytesV1 +
    (kSessionCatalogSnapshotEntryBytesV1 * kSessionCatalogSnapshotCapacityV1);

enum class SessionCatalogRouteStateV1 : std::uint8_t {
    Ready,
    Pending,
    Degraded,
    Unavailable,
};

struct SessionCatalogEntryV1 {
    std::uint64_t handle{0U};
    std::uint8_t active{0U};
    SessionCatalogRouteStateV1 route_state{SessionCatalogRouteStateV1::Unavailable};
    std::uint16_t flags{0U}; // bit 0: session volume is available
    std::int32_t requested_db_q16_16{0};
    std::uint8_t mute{0U};
    std::uint16_t name_bytes{0U};
    std::uint16_t app_bytes{0U};
    std::uint16_t lane_bytes{0U};
    std::uint16_t output_bytes{0U};
    std::array<char, 64U> name{};
    std::array<char, 64U> app{};
    std::array<char, 48U> lane{};
    std::array<char, 48U> output{};
};

struct SessionCatalogSnapshotV1 {
    std::uint64_t sequence{0U};
    std::uint64_t generation{0U};
    std::uint16_t entry_count{0U};
    std::array<SessionCatalogEntryV1, kSessionCatalogSnapshotCapacityV1> entries{};
};

[[nodiscard]] bool encode_session_catalog_snapshot_v1(
    const SessionCatalogSnapshotV1& snapshot,
    std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept;

[[nodiscard]] bool decode_session_catalog_snapshot_v1(
    std::span<const std::uint8_t> payload,
    SessionCatalogSnapshotV1& snapshot) noexcept;

class SessionCatalogSnapshotStoreV1 final {
public:
    SessionCatalogSnapshotStoreV1() noexcept = default;

    [[nodiscard]] bool publish(const SessionCatalogSnapshotV1& snapshot) noexcept;
    // Clears the committed snapshot during runtime teardown/startup so a
    // subsequent request fails closed until a new route refresh publishes one.
    void reset() noexcept;
    [[nodiscard]] bool reply(IpcFrameV1& response) const noexcept;
    [[nodiscard]] bool has_snapshot() const noexcept;
    [[nodiscard]] std::uint64_t sequence() const noexcept;

private:
    mutable std::mutex mutex_{};
    std::array<std::uint8_t, kSessionCatalogSnapshotPayloadBytesV1> payload_{};
    std::size_t payload_bytes_{0U};
    std::uint64_t sequence_{0U};
};

[[nodiscard]] bool session_catalog_snapshot_reply_v1(IpcFrameV1& response,
                                                     void* context) noexcept;

}  // namespace hibiki

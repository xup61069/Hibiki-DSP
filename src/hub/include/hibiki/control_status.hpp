#pragma once

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/control_payloads.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace hibiki {

constexpr std::size_t kControlStatusSnapshotHeaderBytesV1 = 40U;
constexpr std::size_t kControlStatusSnapshotEntryBytesV1 = 224U;
constexpr std::size_t kControlStatusSnapshotCapacityV1 = 8U;
constexpr std::size_t kControlStatusSnapshotPayloadBytesV1 =
    kControlStatusSnapshotHeaderBytesV1 +
    (kControlStatusSnapshotEntryBytesV1 * kControlStatusSnapshotCapacityV1);

enum class ControlRouteHealthStateV1 : std::uint8_t {
    Ready,
    Pending,
    Degraded,
    Bypassed,
    Unavailable,
};

struct ControlRouteHealthEntryV1 {
    std::uint8_t id_bytes{0U};
    std::uint16_t name_bytes{0U};
    std::uint16_t detail_bytes{0U};
    ControlRouteHealthStateV1 state{ControlRouteHealthStateV1::Pending};
    std::uint16_t flags{0U}; // bit 0: requires user action
    std::array<char, 32U> id{};
    std::array<char, 64U> name{};
    std::array<char, 120U> detail{};
};

struct ControlStatusSnapshotV1 {
    std::uint64_t sequence{0U};
    OutputGroupVolumeStateV1 volume{};
    std::uint16_t route_count{0U};
    std::array<ControlRouteHealthEntryV1, kControlStatusSnapshotCapacityV1> routes{};
};

[[nodiscard]] bool encode_control_status_snapshot_v1(
    const ControlStatusSnapshotV1& snapshot,
    std::array<std::uint8_t, kControlStatusSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept;

[[nodiscard]] bool decode_control_status_snapshot_v1(
    std::span<const std::uint8_t> payload,
    ControlStatusSnapshotV1& snapshot) noexcept;

// Bounded control-plane cache for a complete status snapshot. It is analogous
// to DeviceCatalogSnapshotStoreV1: publication is serialized, replies copy a
// whole validated frame, and no RT code or COM callback touches this object.
class ControlStatusSnapshotStoreV1 final {
public:
    ControlStatusSnapshotStoreV1() noexcept = default;

    [[nodiscard]] bool publish(const ControlStatusSnapshotV1& snapshot) noexcept;
    [[nodiscard]] bool reply(IpcFrameV1& response) const noexcept;
    [[nodiscard]] bool has_snapshot() const noexcept;
    [[nodiscard]] std::uint64_t sequence() const noexcept;

private:
    mutable std::mutex mutex_{};
    std::array<std::uint8_t, kControlStatusSnapshotPayloadBytesV1> payload_{};
    std::size_t payload_bytes_{0U};
    std::uint64_t sequence_{0U};
};

[[nodiscard]] bool control_status_snapshot_reply_v1(IpcFrameV1& response,
                                                    void* context) noexcept;

} // namespace hibiki

#pragma once

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/volume_state.hpp"
#include "hibiki/ipc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace hibiki {

// Shared strict UTF-8 predicate for bounded control-plane records. It accepts
// printable Unicode only and rejects control characters, overlong sequences,
// surrogates and truncated code points.
[[nodiscard]] bool is_printable_utf8_v1(std::string_view value) noexcept;

constexpr std::size_t kVolumeNotificationPayloadBytesV1 = 16U;
constexpr std::size_t kGroupedVolumeNotificationPayloadBytesV1 = 48U;
constexpr std::size_t kSceneApplyPayloadBytesV1 = 64U;
constexpr std::size_t kDeviceSwitchEndpointMaxBytesV1 = 260U;
constexpr std::size_t kDeviceSwitchPayloadBytesV1 = 288U;
constexpr std::size_t kDeviceCatalogSnapshotHeaderBytesV1 = 16U;
constexpr std::size_t kDeviceCatalogSnapshotEntryBytesV1 = 416U;
constexpr std::size_t kDeviceCatalogSnapshotCapacityV1 = 32U;
constexpr std::size_t kDeviceCatalogSnapshotPayloadBytesV1 =
    kDeviceCatalogSnapshotHeaderBytesV1 +
    (kDeviceCatalogSnapshotEntryBytesV1 * kDeviceCatalogSnapshotCapacityV1);
constexpr std::size_t kSessionVolumeCommandPayloadBytesV1 = 24U;
constexpr std::size_t kSessionRouteCommandPayloadBytesV1 = 128U;
constexpr std::size_t kSessionRouteCommandLaneMaxBytesV1 = 48U;
constexpr std::size_t kSessionRouteCommandOutputMaxBytesV1 = 48U;
constexpr std::size_t kSessionRouteRuleIdMaxBytesV1 = 64U;
constexpr std::size_t kSessionRouteRuleMatchMaxBytesV1 = 128U;
constexpr std::size_t kSessionRouteRuleRouteMaxBytesV1 = 64U;
constexpr std::size_t kSessionRouteRuleCommandPayloadBytesV1 = 480U;

struct SessionVolumeCommandV1 {
    std::uint64_t handle{0U};
    std::int32_t requested_db_q16_16{0};
    std::uint8_t mute{0U};
    std::uint64_t catalog_sequence{0U};
};

[[nodiscard]] std::array<std::uint8_t, kSessionVolumeCommandPayloadBytesV1>
encode_session_volume_command_v1(const SessionVolumeCommandV1& command) noexcept;
[[nodiscard]] bool decode_session_volume_command_v1(
    std::span<const std::uint8_t> payload,
    SessionVolumeCommandV1& command) noexcept;

struct SessionRouteCommandV1 {
    std::uint64_t handle{0U};
    std::uint64_t catalog_sequence{0U};
    std::uint8_t lane_bytes{0U};
    std::uint8_t output_group_bytes{0U};
    std::array<char, kSessionRouteCommandLaneMaxBytesV1> lane{};
    std::array<char, kSessionRouteCommandOutputMaxBytesV1> output_group{};
};

[[nodiscard]] std::array<std::uint8_t, kSessionRouteCommandPayloadBytesV1>
encode_session_route_command_v1(const SessionRouteCommandV1& command) noexcept;
[[nodiscard]] bool decode_session_route_command_v1(
    std::span<const std::uint8_t> payload,
    SessionRouteCommandV1& command) noexcept;

enum class SessionRouteRuleOperationV1 : std::uint8_t {
    Upsert = 1U,
    Remove = 2U,
    Clear = 3U,
};

enum class SessionRouteRuleGainOwnerV1 : std::uint8_t {
    WindowsSession = 0U,
    HibikiInternal = 1U,
};

// Fixed control-plane rule command. The wire caps are intentionally narrower
// than the in-process rule store so a malformed/oversized profile cannot turn
// a pipe request into an unbounded allocation.
struct SessionRouteRuleCommandV1 {
    std::uint32_t schema_version{1U};
    std::int32_t priority{0};
    std::int32_t makeup_gain_q16_16{0};
    SessionRouteRuleOperationV1 operation{SessionRouteRuleOperationV1::Upsert};
    std::uint8_t enabled{1U};
    SessionRouteRuleGainOwnerV1 gain_owner{SessionRouteRuleGainOwnerV1::WindowsSession};
    std::uint64_t catalog_sequence{0U};
    std::uint16_t rule_id_bytes{0U};
    std::uint16_t app_id_bytes{0U};
    std::uint16_t display_name_bytes{0U};
    std::uint16_t lane_bytes{0U};
    std::uint16_t output_group_bytes{0U};
    std::array<char, kSessionRouteRuleIdMaxBytesV1> rule_id{};
    std::array<char, kSessionRouteRuleMatchMaxBytesV1> app_id{};
    std::array<char, kSessionRouteRuleMatchMaxBytesV1> display_name{};
    std::array<char, kSessionRouteRuleRouteMaxBytesV1> lane{};
    std::array<char, kSessionRouteRuleRouteMaxBytesV1> output_group{};
};

[[nodiscard]] std::array<std::uint8_t, kSessionRouteRuleCommandPayloadBytesV1>
encode_session_route_rule_command_v1(
    const SessionRouteRuleCommandV1& command) noexcept;
[[nodiscard]] bool decode_session_route_rule_command_v1(
    std::span<const std::uint8_t> payload,
    SessionRouteRuleCommandV1& command) noexcept;

// Fixed little-endian control payload shared with apps/control-model. The
// legacy 16-byte form stores dB Q16.16 at offset 0, mute at offset 4, reserved
// bytes 5..7 and generation at offset 8. The 48-byte grouped form appends a
// one-byte UTF-8 output-group length at offset 16 and 31 bytes of zero-padded
// label at offset 17. No native C++ struct layout crosses IPC.
[[nodiscard]] std::array<std::uint8_t, kVolumeNotificationPayloadBytesV1>
encode_volume_notification_payload_v1(const VolumeNotificationV1& notification) noexcept;

[[nodiscard]] bool decode_volume_notification_payload_v1(
    std::span<const std::uint8_t> payload,
    VolumeNotificationV1& notification) noexcept;

struct GroupedVolumeNotificationPayloadV1 {
    std::uint8_t output_group_bytes{0U};
    std::array<char, 31> output_group{};
};

[[nodiscard]] std::array<std::uint8_t, kGroupedVolumeNotificationPayloadBytesV1>
encode_grouped_volume_notification_payload_v1(
    std::string_view output_group,
    const VolumeNotificationV1& notification) noexcept;
[[nodiscard]] bool decode_grouped_volume_notification_payload_v1(
    std::span<const std::uint8_t> payload,
    VolumeNotificationV1& notification,
    GroupedVolumeNotificationPayloadV1& target) noexcept;

struct SceneApplyPayloadV1 {
    std::uint8_t scene_id_bytes{0U};
    std::array<char, 31> scene_id{};
    std::uint8_t output_group_bytes{0U};
    std::array<char, 31> output_group{};
};

[[nodiscard]] bool encode_scene_apply_payload_v1(
    std::string_view scene_id,
    std::string_view output_group,
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1>& payload) noexcept;
[[nodiscard]] bool decode_scene_apply_payload_v1(
    std::span<const std::uint8_t> payload,
    SceneApplyPayloadV1& command) noexcept;

struct DeviceSwitchPayloadV1 {
    std::uint16_t endpoint_id_bytes{0U};
    std::array<char, kDeviceSwitchEndpointMaxBytesV1> endpoint_id{};
    std::uint32_t channels{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t buffer_frames{0U};
    std::uint64_t catalog_sequence{0U};
};

[[nodiscard]] std::array<std::uint8_t, kDeviceSwitchPayloadBytesV1>
encode_device_switch_payload_v1(std::string_view endpoint_id,
                                 std::uint32_t channels,
                                 std::uint32_t sample_rate,
                                 std::uint32_t buffer_frames,
                                 std::uint64_t catalog_sequence) noexcept;
[[nodiscard]] bool decode_device_switch_payload_v1(
    std::span<const std::uint8_t> payload,
    DeviceSwitchPayloadV1& command) noexcept;

// Snapshot wire records are integer/byte-only so this Apache control contract
// does not include the GPL catalog implementation. The engine worker converts
// its descriptors before publishing a DeviceCatalogSnapshot frame.
struct DeviceCatalogSnapshotEntryV1 {
    std::uint16_t endpoint_id_bytes{0U};
    std::array<char, kDeviceSwitchEndpointMaxBytesV1> endpoint_id{};
    std::uint16_t display_name_bytes{0U};
    std::array<char, 128U> display_name{};
    std::uint8_t flow{0U};
    std::uint8_t availability{3U};
    std::uint16_t flags{0U};
    std::uint32_t channels{0U};
    std::uint32_t sample_rate{0U};
    std::uint32_t buffer_frames{0U};
    std::uint64_t last_sequence{0U};
};

struct DeviceCatalogSnapshotV1 {
    std::uint16_t entry_count{0U};
    std::uint64_t catalog_sequence{0U};
    std::array<DeviceCatalogSnapshotEntryV1, kDeviceCatalogSnapshotCapacityV1> entries{};
};

[[nodiscard]] bool encode_device_catalog_snapshot_v1(
    std::span<const DeviceCatalogSnapshotEntryV1> entries,
    std::uint64_t catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept;
[[nodiscard]] bool decode_device_catalog_snapshot_v1(
    std::span<const std::uint8_t> payload,
    DeviceCatalogSnapshotV1& snapshot) noexcept;

struct ControlCommandV1 {
    IpcMessageType type{IpcMessageType::Error};
    std::uint64_t request_id{0U};
    // Grouped volume commands intentionally carry both the dB/mute value and
    // the output-group selector, so these two fields cannot alias.
    VolumeNotificationV1 volume{};
    GroupedVolumeNotificationPayloadV1 volume_target{};
    union {
        SceneApplyPayloadV1 scene;
        DeviceSwitchPayloadV1 device_switch;
        SessionVolumeCommandV1 session_volume;
        SessionRouteCommandV1 session_route;
        SessionRouteRuleCommandV1 session_route_rule;
    };
    bool has_volume_target{false};

    ControlCommandV1() noexcept : volume{} {}
    ControlCommandV1(const ControlCommandV1& other) noexcept {
        std::memcpy(this, &other, sizeof(*this));
    }
    ControlCommandV1& operator=(const ControlCommandV1& other) noexcept {
        if (this != &other) {
            std::memcpy(this, &other, sizeof(*this));
        }
        return *this;
    }
};

[[nodiscard]] bool decode_control_command_v1(const IpcFrameV1& frame,
                                             ControlCommandV1& command) noexcept;
[[nodiscard]] IpcFrameV1 make_ack_frame_v1(const IpcFrameV1& request) noexcept;
[[nodiscard]] IpcFrameV1 make_error_frame_v1(const IpcFrameV1& request) noexcept;

}  // namespace hibiki

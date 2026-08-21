#pragma once

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/volume_state.hpp"
#include "hibiki/ipc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hibiki {

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
    VolumeNotificationV1 volume{};
    GroupedVolumeNotificationPayloadV1 volume_target{};
    bool has_volume_target{false};
    SceneApplyPayloadV1 scene{};
    DeviceSwitchPayloadV1 device_switch{};
};

[[nodiscard]] bool decode_control_command_v1(const IpcFrameV1& frame,
                                             ControlCommandV1& command) noexcept;
[[nodiscard]] IpcFrameV1 make_ack_frame_v1(const IpcFrameV1& request) noexcept;
[[nodiscard]] IpcFrameV1 make_error_frame_v1(const IpcFrameV1& request) noexcept;

}  // namespace hibiki

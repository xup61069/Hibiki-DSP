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

struct ControlCommandV1 {
    IpcMessageType type{IpcMessageType::Error};
    std::uint64_t request_id{0U};
    VolumeNotificationV1 volume{};
    GroupedVolumeNotificationPayloadV1 volume_target{};
    bool has_volume_target{false};
    SceneApplyPayloadV1 scene{};
};

[[nodiscard]] bool decode_control_command_v1(const IpcFrameV1& frame,
                                             ControlCommandV1& command) noexcept;
[[nodiscard]] IpcFrameV1 make_ack_frame_v1(const IpcFrameV1& request) noexcept;
[[nodiscard]] IpcFrameV1 make_error_frame_v1(const IpcFrameV1& request) noexcept;

}  // namespace hibiki

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/control_payloads.hpp"

#include <cmath>
#include <algorithm>

namespace hibiki {
namespace {

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

}  // namespace

std::array<std::uint8_t, kVolumeNotificationPayloadBytesV1>
encode_volume_notification_payload_v1(const VolumeNotificationV1& notification) noexcept {
    std::array<std::uint8_t, kVolumeNotificationPayloadBytesV1> payload{};
    write_u32(payload.data(), static_cast<std::uint32_t>(db_to_q16_16(notification.requested_db)));
    payload[4] = notification.mute ? 1U : 0U;
    write_u64(payload.data() + 8U, notification.generation);
    return payload;
}

bool decode_volume_notification_payload_v1(
    const std::span<const std::uint8_t> payload,
    VolumeNotificationV1& notification) noexcept {
    notification = {};
    if (payload.size() != kVolumeNotificationPayloadBytesV1 ||
        (payload[4] != 0U && payload[4] != 1U) || payload[5] != 0U || payload[6] != 0U ||
        payload[7] != 0U) {
        return false;
    }
    const auto raw_db = static_cast<std::int32_t>(read_u32(payload.data()));
    const auto min_db = static_cast<std::int32_t>(-144 * 65536);
    const auto max_db = static_cast<std::int32_t>(12 * 65536);
    if (raw_db < min_db || raw_db > max_db) return false;
    notification.requested_db = q16_16_to_db(raw_db);
    notification.mute = payload[4] != 0U;
    notification.generation = read_u64(payload.data() + 8U);
    return std::isfinite(notification.requested_db);
}

bool encode_scene_apply_payload_v1(
    const std::string_view scene_id,
    const std::string_view output_group,
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1>& payload) noexcept {
    payload.fill(0U);
    if (scene_id.empty() || scene_id.size() > 31U || output_group.empty() ||
        output_group.size() > 31U ||
        std::any_of(scene_id.begin(), scene_id.end(), [](const char value) {
            return value == '\0' || static_cast<unsigned char>(value) < 0x20U;
        }) ||
        std::any_of(output_group.begin(), output_group.end(), [](const char value) {
            return value == '\0' || static_cast<unsigned char>(value) < 0x20U;
        })) {
        return false;
    }
    payload[0] = static_cast<std::uint8_t>(scene_id.size());
    std::copy(scene_id.begin(), scene_id.end(), payload.begin() + 1);
    payload[32] = static_cast<std::uint8_t>(output_group.size());
    std::copy(output_group.begin(), output_group.end(), payload.begin() + 33);
    return true;
}

bool decode_scene_apply_payload_v1(const std::span<const std::uint8_t> payload,
                                   SceneApplyPayloadV1& command) noexcept {
    command = {};
    if (payload.size() != kSceneApplyPayloadBytesV1 || payload[0] == 0U || payload[0] > 31U ||
        payload[32] == 0U || payload[32] > 31U) {
        return false;
    }
    command.scene_id_bytes = payload[0];
    command.output_group_bytes = payload[32];
    std::copy_n(payload.data() + 1U, command.scene_id_bytes, command.scene_id.data());
    std::copy_n(payload.data() + 33U, command.output_group_bytes, command.output_group.data());
    for (std::size_t index = command.scene_id_bytes; index < command.scene_id.size(); ++index) {
        if (payload[1U + index] != 0U) return false;
    }
    for (std::size_t index = command.output_group_bytes; index < command.output_group.size(); ++index) {
        if (payload[33U + index] != 0U) return false;
    }
    return true;
}

bool decode_control_command_v1(const IpcFrameV1& frame,
                               ControlCommandV1& command) noexcept {
    command = {};
    command.type = frame.header.type;
    command.request_id = frame.header.request_id;
    switch (frame.header.type) {
        case IpcMessageType::Hello:
        case IpcMessageType::GraphCommit:
        case IpcMessageType::GraphRollback:
            return frame.payload.empty();
        case IpcMessageType::VolumeNotification:
            return decode_volume_notification_payload_v1(frame.payload, command.volume);
        case IpcMessageType::SceneApply:
            return decode_scene_apply_payload_v1(frame.payload, command.scene);
        case IpcMessageType::GraphPrepare:
        case IpcMessageType::Ack:
        case IpcMessageType::Error:
            return false;
    }
    return false;
}

IpcFrameV1 make_ack_frame_v1(const IpcFrameV1& request) noexcept {
    IpcFrameV1 response;
    response.header.type = IpcMessageType::Ack;
    response.header.request_id = request.header.request_id;
    return response;
}

IpcFrameV1 make_error_frame_v1(const IpcFrameV1& request) noexcept {
    IpcFrameV1 response;
    response.header.type = IpcMessageType::Error;
    response.header.request_id = request.header.request_id;
    return response;
}

}  // namespace hibiki

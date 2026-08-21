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

bool is_continuation(const unsigned char value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

bool is_printable_utf8(const std::string_view value) noexcept {
    for (std::size_t index = 0U; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::uint32_t codepoint = 0U;
        std::size_t width = 0U;
        if (first <= 0x7FU) {
            if (first < 0x20U) return false;
            codepoint = first;
            width = 1U;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1U >= value.size() ||
                !is_continuation(static_cast<unsigned char>(value[index + 1U]))) {
                return false;
            }
            codepoint = ((first & 0x1FU) << 6U) |
                        (static_cast<unsigned char>(value[index + 1U]) & 0x3FU);
            width = 2U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2U >= value.size()) return false;
            const auto second = static_cast<unsigned char>(value[index + 1U]);
            const auto third = static_cast<unsigned char>(value[index + 2U]);
            const bool second_valid = first == 0xE0U ? second >= 0xA0U && second <= 0xBFU
                                                    : first == 0xEDU ? second <= 0x9FU
                                                                      : second >= 0x80U && second <= 0xBFU;
            if (!second_valid || !is_continuation(third)) return false;
            codepoint = ((first & 0x0FU) << 12U) | ((second & 0x3FU) << 6U) |
                        (third & 0x3FU);
            width = 3U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3U >= value.size()) return false;
            const auto second = static_cast<unsigned char>(value[index + 1U]);
            const auto third = static_cast<unsigned char>(value[index + 2U]);
            const auto fourth = static_cast<unsigned char>(value[index + 3U]);
            const bool second_valid = first == 0xF0U ? second >= 0x90U && second <= 0xBFU
                                                    : first == 0xF4U ? second <= 0x8FU
                                                                      : second >= 0x80U && second <= 0xBFU;
            if (!second_valid || !is_continuation(third) || !is_continuation(fourth)) {
                return false;
            }
            codepoint = ((first & 0x07U) << 18U) | ((second & 0x3FU) << 12U) |
                        ((third & 0x3FU) << 6U) | (fourth & 0x3FU);
            width = 4U;
        } else {
            return false;
        }
        if (codepoint < 0x20U || (codepoint >= 0x7FU && codepoint <= 0x9FU)) return false;
        index += width;
    }
    return true;
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

std::array<std::uint8_t, kGroupedVolumeNotificationPayloadBytesV1>
encode_grouped_volume_notification_payload_v1(
    const std::string_view output_group,
    const VolumeNotificationV1& notification) noexcept {
    std::array<std::uint8_t, kGroupedVolumeNotificationPayloadBytesV1> payload{};
    if (output_group.empty() || output_group.size() > 31U ||
        !is_printable_utf8(output_group)) {
        return payload;
    }
    const auto volume_payload = encode_volume_notification_payload_v1(notification);
    std::copy(volume_payload.begin(), volume_payload.end(), payload.begin());
    payload[16] = static_cast<std::uint8_t>(output_group.size());
    std::copy(output_group.begin(), output_group.end(), payload.begin() + 17U);
    return payload;
}

bool decode_grouped_volume_notification_payload_v1(
    const std::span<const std::uint8_t> payload,
    VolumeNotificationV1& notification,
    GroupedVolumeNotificationPayloadV1& target) noexcept {
    notification = {};
    target = {};
    if (payload.size() != kGroupedVolumeNotificationPayloadBytesV1 || payload[16] == 0U ||
        payload[16] > target.output_group.size() ||
        !decode_volume_notification_payload_v1(payload.first(kVolumeNotificationPayloadBytesV1),
                                               notification)) {
        return false;
    }
    target.output_group_bytes = payload[16];
    std::copy_n(payload.data() + 17U, target.output_group_bytes, target.output_group.data());
    for (std::size_t index = target.output_group_bytes; index < target.output_group.size(); ++index) {
        if (payload[17U + index] != 0U) return false;
    }
    return is_printable_utf8(
        std::string_view(target.output_group.data(), target.output_group_bytes));
}

bool encode_scene_apply_payload_v1(
    const std::string_view scene_id,
    const std::string_view output_group,
    std::array<std::uint8_t, kSceneApplyPayloadBytesV1>& payload) noexcept {
    payload.fill(0U);
    if (scene_id.empty() || scene_id.size() > 31U || output_group.empty() ||
        output_group.size() > 31U || !is_printable_utf8(scene_id) ||
        !is_printable_utf8(output_group)) {
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
    return is_printable_utf8(
               std::string_view(command.scene_id.data(), command.scene_id_bytes)) &&
           is_printable_utf8(
               std::string_view(command.output_group.data(), command.output_group_bytes));
}

std::array<std::uint8_t, kDeviceSwitchPayloadBytesV1>
encode_device_switch_payload_v1(const std::string_view endpoint_id,
                                const std::uint32_t channels,
                                const std::uint32_t sample_rate,
                                const std::uint32_t buffer_frames,
                                const std::uint64_t catalog_sequence) noexcept {
    std::array<std::uint8_t, kDeviceSwitchPayloadBytesV1> payload{};
    if (endpoint_id.empty() || endpoint_id.size() > kDeviceSwitchEndpointMaxBytesV1 ||
        !is_printable_utf8(endpoint_id)) {
        return payload;
    }
    payload[0] = static_cast<std::uint8_t>(endpoint_id.size() & 0xffU);
    payload[1] = static_cast<std::uint8_t>((endpoint_id.size() >> 8U) & 0xffU);
    std::copy(endpoint_id.begin(), endpoint_id.end(),
              payload.begin() + static_cast<std::ptrdiff_t>(2U));
    write_u32(payload.data() + 264U, channels);
    write_u32(payload.data() + 268U, sample_rate);
    write_u32(payload.data() + 272U, buffer_frames);
    write_u64(payload.data() + 280U, catalog_sequence);
    return payload;
}

bool decode_device_switch_payload_v1(const std::span<const std::uint8_t> payload,
                                     DeviceSwitchPayloadV1& command) noexcept {
    command = {};
    if (payload.size() != kDeviceSwitchPayloadBytesV1) return false;
    const auto endpoint_bytes = static_cast<std::size_t>(payload[0]) |
                                (static_cast<std::size_t>(payload[1]) << 8U);
    if (endpoint_bytes == 0U || endpoint_bytes > kDeviceSwitchEndpointMaxBytesV1 ||
        payload[262U] != 0U || payload[263U] != 0U || payload[276U] != 0U ||
        payload[277U] != 0U || payload[278U] != 0U || payload[279U] != 0U) {
        return false;
    }
    for (std::size_t index = endpoint_bytes; index < kDeviceSwitchEndpointMaxBytesV1; ++index) {
        if (payload[2U + index] != 0U) return false;
    }
    const std::string_view endpoint(reinterpret_cast<const char*>(payload.data() + 2U),
                                    endpoint_bytes);
    if (!is_printable_utf8(endpoint)) return false;
    const auto channels = read_u32(payload.data() + 264U);
    const auto sample_rate = read_u32(payload.data() + 268U);
    const auto buffer_frames = read_u32(payload.data() + 272U);
    if ((channels != 1U && channels != 2U && channels != 6U && channels != 8U) ||
        (sample_rate != 44100U && sample_rate != 48000U && sample_rate != 96000U &&
         sample_rate != 192000U) ||
        buffer_frames < 16U || buffer_frames > 4096U) {
        return false;
    }
    command.endpoint_id_bytes = static_cast<std::uint16_t>(endpoint_bytes);
    std::copy_n(endpoint.data(), endpoint_bytes, command.endpoint_id.data());
    command.channels = channels;
    command.sample_rate = sample_rate;
    command.buffer_frames = buffer_frames;
    command.catalog_sequence = read_u64(payload.data() + 280U);
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
            if (frame.payload.size() == kVolumeNotificationPayloadBytesV1) {
                return decode_volume_notification_payload_v1(frame.payload, command.volume);
            }
            command.has_volume_target = decode_grouped_volume_notification_payload_v1(
                frame.payload, command.volume, command.volume_target);
            return command.has_volume_target;
        case IpcMessageType::SceneApply:
            return decode_scene_apply_payload_v1(frame.payload, command.scene);
        case IpcMessageType::DeviceSwitch:
            return decode_device_switch_payload_v1(frame.payload, command.device_switch);
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

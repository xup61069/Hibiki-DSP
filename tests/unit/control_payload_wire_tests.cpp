// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
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

using hibiki::DeviceSwitchPayloadV1;
using hibiki::GroupedVolumeNotificationPayloadV1;
using hibiki::IrPrepareCommandV1;
using hibiki::SceneApplyPayloadV1;
using hibiki::SessionRouteCommandV1;
using hibiki::SessionVolumeCommandV1;
using hibiki::VolumeNotificationV1;

template <std::size_t N>
[[nodiscard]] bool all_zero(const std::array<std::uint8_t, N>& bytes) noexcept
{
    return std::all_of(bytes.begin(), bytes.end(), [](const std::uint8_t byte) {
        return byte == 0U;
    });
}

template <std::size_t N>
void write_u16_le(std::array<std::uint8_t, N>& bytes,
                  const std::size_t offset,
                  const std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

template <std::size_t N>
void write_u32_le(std::array<std::uint8_t, N>& bytes,
                  const std::size_t offset,
                  const std::uint32_t value) noexcept
{
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU);
    }
}

template <std::size_t N>
void write_u64_le(std::array<std::uint8_t, N>& bytes,
                  const std::size_t offset,
                  const std::uint64_t value) noexcept
{
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU);
    }
}

[[nodiscard]] SessionRouteCommandV1 make_route(const std::string_view lane,
                                                const std::string_view output_group) noexcept
{
    auto command = SessionRouteCommandV1{};
    command.handle = 0x0000000200000001ULL;
    command.catalog_sequence = 17U;
    command.lane_bytes = static_cast<std::uint8_t>(lane.size());
    command.output_group_bytes = static_cast<std::uint8_t>(output_group.size());
    std::copy(lane.begin(), lane.end(), command.lane.begin());
    std::copy(output_group.begin(), output_group.end(), command.output_group.begin());
    return command;
}

[[nodiscard]] IrPrepareCommandV1 make_ir(const std::uint8_t mode,
                                          const std::int32_t strength,
                                          const std::uint32_t sample_rate,
                                          const std::uint32_t channels,
                                          const std::string_view path) noexcept
{
    auto command = IrPrepareCommandV1{};
    command.mode = mode;
    command.strength_q16_16 = strength;
    command.expected_sample_rate = sample_rate;
    command.expected_channels = channels;
    command.path_bytes = static_cast<std::uint16_t>(path.size());
    std::copy(path.begin(), path.end(), command.path.begin());
    return command;
}

[[nodiscard]] std::array<std::uint8_t, hibiki::kDeviceSwitchPayloadBytesV1>
encode_device(const std::string_view endpoint,
              const std::uint32_t channels = 2U,
              const std::uint32_t sample_rate = 48000U,
              const std::uint32_t buffer_frames = 128U)
{
    return hibiki::encode_device_switch_payload_v1(
        endpoint, channels, sample_rate, buffer_frames, 23U);
}

}  // namespace

int main()
{
    // ---- shared printable UTF-8: special lead-byte boundaries -----------
    {
        CHECK(!hibiki::is_printable_utf8_v1(std::string_view("\xED\x40\x80", 3U)));
        CHECK(hibiki::is_printable_utf8_v1(std::string_view("\xED\x9F\xBF", 3U)));
        CHECK(!hibiki::is_printable_utf8_v1(std::string_view("\xED\xA0\x80", 3U)));

        CHECK(!hibiki::is_printable_utf8_v1(std::string_view("\xF4\x40\x80\x80", 4U)));
        CHECK(hibiki::is_printable_utf8_v1(std::string_view("\xF4\x8F\xBF\xBF", 4U)));
        CHECK(!hibiki::is_printable_utf8_v1(std::string_view("\xF4\x90\x80\x80", 4U)));
    }

    // ---- volume notification: dB, mute, generation and reserved bytes ---
    {
        auto notification = VolumeNotificationV1{};
        notification.requested_db = -144.0;
        notification.mute = true;
        notification.generation = 0x0102030405060708ULL;
        const auto payload = hibiki::encode_volume_notification_payload_v1(notification);
        auto decoded = VolumeNotificationV1{};
        CHECK(hibiki::decode_volume_notification_payload_v1(payload, decoded));
        CHECK(decoded.requested_db == -144.0 && decoded.mute &&
              decoded.generation == notification.generation);

        notification.requested_db = 12.0;
        notification.mute = false;
        const auto maximum = hibiki::encode_volume_notification_payload_v1(notification);
        CHECK(hibiki::decode_volume_notification_payload_v1(maximum, decoded));
        CHECK(decoded.requested_db == 12.0 && !decoded.mute);

        for (std::size_t index = 5U; index <= 7U; ++index) {
            auto malformed = payload;
            malformed[index] = 1U;
            CHECK(!hibiki::decode_volume_notification_payload_v1(malformed, decoded));
        }
        auto malformed = payload;
        malformed[4U] = 2U;
        CHECK(!hibiki::decode_volume_notification_payload_v1(malformed, decoded));
        malformed = payload;
        write_u32_le(malformed, 0U, static_cast<std::uint32_t>(-145 * 65536));
        CHECK(!hibiki::decode_volume_notification_payload_v1(malformed, decoded));
        malformed = maximum;
        write_u32_le(malformed, 0U, static_cast<std::uint32_t>(13 * 65536));
        CHECK(!hibiki::decode_volume_notification_payload_v1(malformed, decoded));
        malformed = payload;
        write_u64_le(malformed, 8U, 0U);
        decoded.requested_db = 12.0;
        decoded.mute = true;
        decoded.generation = 99U;
        CHECK(!hibiki::decode_volume_notification_payload_v1(malformed, decoded));
        CHECK(decoded.requested_db == -60.0 && !decoded.mute && decoded.generation == 0U);
        for (const auto requested_db : {std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity(),
                                        -145.0, 13.0}) {
            notification.requested_db = requested_db;
            notification.generation = 1U;
            CHECK(all_zero(hibiki::encode_volume_notification_payload_v1(notification)));
        }
        CHECK(!hibiki::decode_volume_notification_payload_v1(
            std::span<const std::uint8_t>(payload.data(), payload.size() - 1U), decoded));
    }

    // ---- grouped volume notification: label bounds and padding ----------
    {
        auto notification = VolumeNotificationV1{};
        notification.requested_db = -6.0;
        notification.mute = true;
        notification.generation = 9U;
        const auto payload = hibiki::encode_grouped_volume_notification_payload_v1(
            "main", notification);
        auto decoded = VolumeNotificationV1{};
        auto target = GroupedVolumeNotificationPayloadV1{};
        CHECK(hibiki::decode_grouped_volume_notification_payload_v1(
            payload, decoded, target));
        CHECK(decoded.mute && decoded.generation == 9U &&
              target.output_group_bytes == 4U &&
              std::string_view(target.output_group.data(), target.output_group_bytes) == "main");

        const auto maximum_label = std::string(31U, 'g');
        const auto maximum = hibiki::encode_grouped_volume_notification_payload_v1(
            maximum_label, notification);
        CHECK(!all_zero(maximum));
        CHECK(hibiki::decode_grouped_volume_notification_payload_v1(
            maximum, decoded, target));
        CHECK(target.output_group_bytes == 31U);

        CHECK(all_zero(hibiki::encode_grouped_volume_notification_payload_v1(
            "", notification)));
        CHECK(all_zero(hibiki::encode_grouped_volume_notification_payload_v1(
            std::string(32U, 'g'), notification)));
        CHECK(all_zero(hibiki::encode_grouped_volume_notification_payload_v1(
            std::string_view("ma\x01n", 4U), notification)));

        const auto grouped_outputs_are_neutral = [&decoded, &target]() noexcept {
            return decoded.requested_db == -60.0 && !decoded.mute && decoded.generation == 0U &&
                   target.output_group_bytes == 0U &&
                   std::all_of(target.output_group.begin(), target.output_group.end(),
                               [](const char byte) { return byte == '\0'; });
        };
        auto malformed = payload;
        malformed[17U + 4U] = 1U;
        CHECK(!hibiki::decode_grouped_volume_notification_payload_v1(
            malformed, decoded, target));
        CHECK(grouped_outputs_are_neutral());
        malformed = payload;
        malformed[16U] = 2U;
        malformed[17U] = 0xC3U;
        malformed[18U] = 0x28U;
        std::fill(malformed.begin() + 19U, malformed.end(), std::uint8_t{0U});
        decoded.requested_db = 12.0;
        decoded.mute = true;
        decoded.generation = 99U;
        target.output_group_bytes = 4U;
        std::fill(target.output_group.begin(), target.output_group.end(), 'x');
        CHECK(!hibiki::decode_grouped_volume_notification_payload_v1(
            malformed, decoded, target));
        CHECK(grouped_outputs_are_neutral());
        malformed = payload;
        malformed[16U] = 32U;
        CHECK(!hibiki::decode_grouped_volume_notification_payload_v1(
            malformed, decoded, target));
        CHECK(grouped_outputs_are_neutral());
        malformed = payload;
        malformed[17U] = 0x01U;
        CHECK(!hibiki::decode_grouped_volume_notification_payload_v1(
            malformed, decoded, target));
        CHECK(grouped_outputs_are_neutral());
        malformed = payload;
        write_u64_le(malformed, 8U, 0U);
        decoded.requested_db = 12.0;
        decoded.mute = true;
        decoded.generation = 99U;
        target.output_group_bytes = 4U;
        CHECK(!hibiki::decode_grouped_volume_notification_payload_v1(
            malformed, decoded, target));
        CHECK(grouped_outputs_are_neutral());
        notification.generation = 0U;
        CHECK(all_zero(hibiki::encode_grouped_volume_notification_payload_v1(
            "main", notification)));
    }

    // ---- session volume command: identity, Q16.16 and reserved bytes -----
    {
        auto command = SessionVolumeCommandV1{};
        command.handle = 0x0000000300000002ULL;
        command.requested_db_q16_16 = -144 * 65536;
        command.mute = 1U;
        command.catalog_sequence = 21U;
        const auto payload = hibiki::encode_session_volume_command_v1(command);
        auto decoded = SessionVolumeCommandV1{};
        CHECK(hibiki::decode_session_volume_command_v1(payload, decoded));
        CHECK(decoded.handle == command.handle &&
              decoded.requested_db_q16_16 == command.requested_db_q16_16 &&
              decoded.mute == 1U && decoded.catalog_sequence == 21U);

        command.requested_db_q16_16 = hibiki::kSessionVolumeMaxDbQ16_16V1;
        command.mute = 0U;
        const auto maximum = hibiki::encode_session_volume_command_v1(command);
        CHECK(hibiki::decode_session_volume_command_v1(maximum, decoded));
        CHECK(decoded.requested_db_q16_16 == hibiki::kSessionVolumeMaxDbQ16_16V1 &&
              decoded.mute == 0U);
        CHECK(hibiki::is_valid_session_volume_db_q16_16_v1(
                  hibiki::kSessionVolumeMinDbQ16_16V1) &&
              hibiki::is_valid_session_volume_db_q16_16_v1(
                  hibiki::kSessionVolumeMaxDbQ16_16V1) &&
              !hibiki::is_valid_session_volume_db_q16_16_v1(1));

        const auto session_volume_output_is_neutral = [&decoded]() noexcept {
            return decoded.handle == 0U && decoded.requested_db_q16_16 == 0 &&
                   decoded.mute == 0U && decoded.catalog_sequence == 0U;
        };

        auto invalid = command;
        invalid.handle = 0U;
        CHECK(all_zero(hibiki::encode_session_volume_command_v1(invalid)));
        invalid = command;
        invalid.catalog_sequence = 0U;
        CHECK(all_zero(hibiki::encode_session_volume_command_v1(invalid)));
        invalid = command;
        invalid.mute = 2U;
        CHECK(all_zero(hibiki::encode_session_volume_command_v1(invalid)));
        invalid = command;
        invalid.requested_db_q16_16 = -145 * 65536;
        CHECK(all_zero(hibiki::encode_session_volume_command_v1(invalid)));
        invalid.requested_db_q16_16 = 1 * 65536;
        CHECK(all_zero(hibiki::encode_session_volume_command_v1(invalid)));

        auto malformed = maximum;
        write_u64_le(malformed, 0U, 0U);
        CHECK(!hibiki::decode_session_volume_command_v1(malformed, decoded));
        CHECK(session_volume_output_is_neutral());
        malformed = maximum;
        write_u64_le(malformed, 16U, 0U);
        CHECK(!hibiki::decode_session_volume_command_v1(malformed, decoded));
        CHECK(session_volume_output_is_neutral());
        malformed = maximum;
        malformed[12U] = 2U;
        CHECK(!hibiki::decode_session_volume_command_v1(malformed, decoded));
        CHECK(session_volume_output_is_neutral());
        malformed = maximum;
        write_u32_le(malformed, 8U, 1U * 65536U);
        CHECK(!hibiki::decode_session_volume_command_v1(malformed, decoded));
        CHECK(session_volume_output_is_neutral());
        for (std::size_t index = 13U; index <= 15U; ++index) {
            malformed = maximum;
            malformed[index] = 1U;
            CHECK(!hibiki::decode_session_volume_command_v1(malformed, decoded));
            CHECK(session_volume_output_is_neutral());
        }
        CHECK(!hibiki::decode_session_volume_command_v1(
            std::span<const std::uint8_t>(maximum.data(), maximum.size() - 1U), decoded));
        CHECK(session_volume_output_is_neutral());
    }

    // ---- session route command: two bounded printable labels -------------
    {
        const auto command = make_route("game", "surround");
        const auto payload = hibiki::encode_session_route_command_v1(command);
        auto decoded = SessionRouteCommandV1{};
        CHECK(hibiki::decode_session_route_command_v1(payload, decoded));
        CHECK(decoded.handle == command.handle &&
              decoded.catalog_sequence == command.catalog_sequence &&
              std::string_view(decoded.lane.data(), decoded.lane_bytes) == "game" &&
              std::string_view(decoded.output_group.data(), decoded.output_group_bytes) ==
                  "surround");

        const auto maximum = make_route(std::string(48U, 'l'), std::string(48U, 'o'));
        const auto maximum_payload = hibiki::encode_session_route_command_v1(maximum);
        CHECK(hibiki::decode_session_route_command_v1(maximum_payload, decoded));
        CHECK(decoded.lane_bytes == 48U && decoded.output_group_bytes == 48U);

        const auto route_output_is_neutral = [&decoded]() noexcept {
            return decoded.handle == 0U && decoded.catalog_sequence == 0U &&
                   decoded.lane_bytes == 0U && decoded.output_group_bytes == 0U &&
                   std::all_of(decoded.lane.begin(), decoded.lane.end(),
                               [](const char value) { return value == '\0'; }) &&
                   std::all_of(decoded.output_group.begin(), decoded.output_group.end(),
                               [](const char value) { return value == '\0'; });
        };

        auto invalid = make_route(std::string(49U, 'l'), "main");
        CHECK(all_zero(hibiki::encode_session_route_command_v1(invalid)));
        invalid = make_route("game", std::string(49U, 'o'));
        CHECK(all_zero(hibiki::encode_session_route_command_v1(invalid)));
        invalid = make_route("ma\x01n", "main");
        CHECK(all_zero(hibiki::encode_session_route_command_v1(invalid)));
        invalid = make_route("game", "ma\x01n");
        CHECK(all_zero(hibiki::encode_session_route_command_v1(invalid)));

        auto malformed = payload;
        write_u64_le(malformed, 0U, 0U);
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        malformed = payload;
        write_u64_le(malformed, 8U, 0U);
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        for (const std::size_t index : {18U, 19U}) {
            malformed = payload;
            malformed[index] = 1U;
            CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
            CHECK(route_output_is_neutral());
        }
        malformed = payload;
        malformed[20U + 4U] = 1U;
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        malformed = payload;
        malformed[68U + 7U] = 1U;
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        malformed = payload;
        malformed[116U] = 1U;
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        malformed = payload;
        malformed[20U] = 0x01U;
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        malformed = payload;
        malformed[20U] = 0xc3U;
        malformed[21U] = 0x28U;
        CHECK(!hibiki::decode_session_route_command_v1(malformed, decoded));
        CHECK(route_output_is_neutral());
        CHECK(!hibiki::decode_session_route_command_v1(
            std::span<const std::uint8_t>(payload.data(), payload.size() - 1U), decoded));
        CHECK(route_output_is_neutral());
    }

    // ---- IR prepare command: modes, optional expectations and path bounds -
    {
        constexpr std::string_view path = "C:/Hibiki/measurements/movie.wav";
        for (std::uint8_t mode = 0U; mode <= 2U; ++mode) {
            const auto command = make_ir(mode, 65536, 8000U, 1U, path);
            const auto payload = hibiki::encode_ir_prepare_command_v1(command);
            auto decoded = IrPrepareCommandV1{};
            CHECK(hibiki::decode_ir_prepare_command_v1(payload, decoded));
            CHECK(decoded.mode == mode && decoded.strength_q16_16 == 65536 &&
                  decoded.expected_sample_rate == 8000U && decoded.expected_channels == 1U);
        }
        const auto bypass = make_ir(3U, 0, 192000U, 8U, path);
        const auto bypass_payload = hibiki::encode_ir_prepare_command_v1(bypass);
        auto decoded = IrPrepareCommandV1{};
        CHECK(hibiki::decode_ir_prepare_command_v1(bypass_payload, decoded));
        CHECK(decoded.mode == 3U && decoded.strength_q16_16 == 0 &&
              decoded.expected_sample_rate == 192000U && decoded.expected_channels == 8U);

        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(3U, 1, 48000U, 2U, path))));
        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(4U, 0, 48000U, 2U, path))));
        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, -1, 48000U, 2U, path))));
        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, 65537, 48000U, 2U, path))));

        for (const std::uint32_t rate : {0U, 8000U, 192000U}) {
            const auto valid = hibiki::encode_ir_prepare_command_v1(
                make_ir(0U, 0, rate, 0U, "p"));
            CHECK(hibiki::decode_ir_prepare_command_v1(valid, decoded));
        }
        for (const std::uint32_t rate : {7999U, 192001U}) {
            CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
                make_ir(0U, 0, rate, 2U, path))));
        }
        for (const std::uint32_t channels : {0U, 1U, 8U}) {
            const auto valid = hibiki::encode_ir_prepare_command_v1(
                make_ir(0U, 0, 48000U, channels, "p"));
            CHECK(hibiki::decode_ir_prepare_command_v1(valid, decoded));
        }
        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, 0, 48000U, 9U, path))));

        const auto one_byte = hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, 0, 48000U, 2U, "p"));
        CHECK(hibiki::decode_ir_prepare_command_v1(one_byte, decoded));
        const auto maximum = hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, 0, 48000U, 2U, std::string(260U, 'p')));
        CHECK(hibiki::decode_ir_prepare_command_v1(maximum, decoded));
        CHECK(decoded.path_bytes == 260U);
        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, 0, 48000U, 2U, std::string(261U, 'p')))));
        CHECK(all_zero(hibiki::encode_ir_prepare_command_v1(
            make_ir(0U, 0, 48000U, 2U, std::string_view("a\x01", 2U)))));

        const auto ir_prepare_output_is_neutral = [&decoded]() noexcept {
            const auto neutral = IrPrepareCommandV1{};
            return decoded.schema_version == neutral.schema_version &&
                   decoded.mode == neutral.mode &&
                   decoded.strength_q16_16 == neutral.strength_q16_16 &&
                   decoded.expected_sample_rate == neutral.expected_sample_rate &&
                   decoded.expected_channels == neutral.expected_channels &&
                   decoded.path_bytes == neutral.path_bytes &&
                   std::all_of(decoded.path.begin(), decoded.path.end(),
                               [](const char byte) { return byte == '\0'; });
        };
        auto malformed = one_byte;
        malformed[24U] = 0x01U;
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        CHECK(ir_prepare_output_is_neutral());
        malformed = one_byte;
        malformed[24U + 1U] = 1U;
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        malformed = one_byte;
        malformed[284U] = 1U;
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        malformed = one_byte;
        malformed[5U] = 1U;
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        malformed = one_byte;
        write_u32_le(malformed, 0U, 2U);
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        malformed = one_byte;
        malformed[4U] = 4U;
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        malformed = one_byte;
        write_u16_le(malformed, 20U, 0U);
        CHECK(!hibiki::decode_ir_prepare_command_v1(malformed, decoded));
        CHECK(!hibiki::decode_ir_prepare_command_v1(
            std::span<const std::uint8_t>(one_byte.data(), one_byte.size() - 1U), decoded));
    }

    // ---- scene apply payload: both IDs are bounded printable byte strings -
    {
        std::array<std::uint8_t, hibiki::kSceneApplyPayloadBytesV1> payload{};
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        auto decoded = SceneApplyPayloadV1{};
        CHECK(hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(decoded.scene_id_bytes == 1U && decoded.output_group_bytes == 1U &&
              decoded.scene_id[0] == 'g' && decoded.output_group[0] == 'm');

        const auto maximum_scene = std::string(31U, 's');
        const auto maximum_output = std::string(31U, 'o');
        CHECK(hibiki::encode_scene_apply_payload_v1(
            maximum_scene, maximum_output, payload));
        CHECK(hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(decoded.scene_id_bytes == 31U && decoded.output_group_bytes == 31U);
        CHECK(!hibiki::encode_scene_apply_payload_v1(
            std::string(32U, 's'), "main", payload));
        CHECK(!hibiki::encode_scene_apply_payload_v1(
            "game", std::string(32U, 'o'), payload));
        CHECK(!hibiki::encode_scene_apply_payload_v1("", "main", payload));
        CHECK(!hibiki::encode_scene_apply_payload_v1(
            std::string_view("g\x01", 2U), "main", payload));

        const auto scene_apply_output_is_neutral = [&decoded]() noexcept {
            return decoded.scene_id_bytes == 0U && decoded.output_group_bytes == 0U &&
                   std::all_of(decoded.scene_id.begin(), decoded.scene_id.end(),
                               [](const char byte) { return byte == '\0'; }) &&
                   std::all_of(decoded.output_group.begin(), decoded.output_group.end(),
                               [](const char byte) { return byte == '\0'; });
        };
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        payload[1U + 1U] = 1U;
        CHECK(!hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(scene_apply_output_is_neutral());
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        payload[33U + 1U] = 1U;
        CHECK(!hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(scene_apply_output_is_neutral());
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        payload[0U] = 2U;
        payload[1U] = 0xC3U;
        payload[2U] = 0x28U;
        std::fill(payload.begin() + 3U, payload.begin() + 32U, std::uint8_t{0U});
        decoded.scene_id_bytes = 1U;
        decoded.output_group_bytes = 1U;
        std::fill(decoded.scene_id.begin(), decoded.scene_id.end(), 's');
        std::fill(decoded.output_group.begin(), decoded.output_group.end(), 'o');
        CHECK(!hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(scene_apply_output_is_neutral());
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        payload[1U] = 0x01U;
        CHECK(!hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(scene_apply_output_is_neutral());
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        payload[0U] = 32U;
        CHECK(!hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(scene_apply_output_is_neutral());
        CHECK(hibiki::encode_scene_apply_payload_v1("g", "m", payload));
        payload[32U] = 32U;
        CHECK(!hibiki::decode_scene_apply_payload_v1(payload, decoded));
        CHECK(scene_apply_output_is_neutral());
        CHECK(!hibiki::decode_scene_apply_payload_v1(
            std::span<const std::uint8_t>(payload.data(), payload.size() - 1U), decoded));
        CHECK(scene_apply_output_is_neutral());
    }

    // ---- device switch payload: endpoint and enumerated device parameters -
    {
        const auto payload = encode_device("endpoint-render", 2U, 48000U, 128U);
        auto decoded = DeviceSwitchPayloadV1{};
        CHECK(hibiki::decode_device_switch_payload_v1(payload, decoded));
        CHECK(decoded.endpoint_id_bytes == 15U && decoded.channels == 2U &&
              decoded.sample_rate == 48000U && decoded.buffer_frames == 128U &&
              decoded.catalog_sequence == 23U);

        const auto maximum_endpoint = encode_device(std::string(260U, 'e'), 8U, 192000U, 4096U);
        CHECK(hibiki::decode_device_switch_payload_v1(maximum_endpoint, decoded));
        CHECK(decoded.endpoint_id_bytes == 260U && decoded.channels == 8U &&
              decoded.sample_rate == 192000U && decoded.buffer_frames == 4096U);
        CHECK(hibiki::decode_device_switch_payload_v1(
            encode_device("e", 2U, 44100U, 16U), decoded));
        for (const std::uint32_t channels : {1U, 2U, 6U, 8U}) {
            CHECK(hibiki::decode_device_switch_payload_v1(
                encode_device("e", channels), decoded));
        }
        for (const std::uint32_t rate : {44100U, 48000U, 96000U, 192000U}) {
            CHECK(hibiki::decode_device_switch_payload_v1(
                encode_device("e", 2U, rate), decoded));
        }

        CHECK(all_zero(encode_device(std::string(261U, 'e'))));
        CHECK(all_zero(hibiki::encode_device_switch_payload_v1(
            std::string_view("e\x01", 2U), 2U, 48000U, 128U, 23U)));
        CHECK(all_zero(hibiki::encode_device_switch_payload_v1(
            "e", 2U, 48000U, 128U, 0U)));
        auto malformed = payload;
        malformed[2U + 15U] = 1U;
        CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        malformed = payload;
        malformed[262U] = 1U;
        CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        malformed = payload;
        malformed[276U] = 1U;
        CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        for (const std::uint32_t channels : {0U, 3U}) {
            malformed = payload;
            write_u32_le(malformed, 264U, channels);
            CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        }
        for (const std::uint32_t rate : {0U, 1234U}) {
            malformed = payload;
            write_u32_le(malformed, 268U, rate);
            CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        }
        for (const std::uint32_t frames : {15U, 4097U}) {
            malformed = payload;
            write_u32_le(malformed, 272U, frames);
            CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        }
        malformed = payload;
        write_u64_le(malformed, 280U, 0U);
        decoded.endpoint_id_bytes = 1U;
        decoded.channels = 2U;
        decoded.sample_rate = 48000U;
        decoded.buffer_frames = 128U;
        decoded.catalog_sequence = 23U;
        CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        CHECK(decoded.endpoint_id_bytes == 0U && decoded.channels == 0U &&
              decoded.sample_rate == 0U && decoded.buffer_frames == 0U &&
              decoded.catalog_sequence == 0U);
        malformed = payload;
        malformed[2U] = 0x01U;
        CHECK(!hibiki::decode_device_switch_payload_v1(malformed, decoded));
        CHECK(!hibiki::decode_device_switch_payload_v1(
            std::span<const std::uint8_t>(payload.data(), payload.size() - 1U), decoded));
    }

    std::puts("control payload wire tests passed");
    return 0;
}

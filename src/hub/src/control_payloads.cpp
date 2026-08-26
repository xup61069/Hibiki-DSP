// SPDX-License-Identifier: Apache-2.0

#include "hibiki/control_payloads.hpp"

#include <cmath>
#include <algorithm>

namespace hibiki {
namespace {

void write_u16(std::uint8_t* bytes, const std::uint16_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

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

void write_f64_bits(std::uint8_t* bytes, double value) noexcept;
double read_f64_bits(const std::uint8_t* bytes) noexcept;

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

bool encode_eq_visual_snapshot_v1(
    const EqVisualSnapshotV1& snapshot,
    std::array<std::uint8_t, kEqVisualSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    payload.fill(0U);
    payload_bytes = 0U;
    if (snapshot.sequence == 0U || snapshot.source == 0U || snapshot.source > 2U) {
        return false;
    }

    const auto is_zero_tail = [&snapshot](const std::size_t index) noexcept {
        for (std::size_t next = index; next < snapshot.points.size(); ++next) {
            if (snapshot.points[next].frequency_hz != 0.0 ||
                snapshot.points[next].gain_db != 0.0) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t index = 0U; index < snapshot.points.size(); ++index) {
        const auto& point = snapshot.points[index];
        if (!std::isfinite(point.frequency_hz) || !std::isfinite(point.gain_db)) return false;
        // Zero padding must be contiguous from the first all-zero pair to the
        // end; real points must be strictly increasing in frequency.
        if (point.frequency_hz == 0.0 && point.gain_db == 0.0 &&
            is_zero_tail(index + 1U)) {
            break;
        }
        if (index != 0U &&
            !(snapshot.points[index - 1U].frequency_hz < point.frequency_hz)) {
            return false;
        }
        if (point.frequency_hz >= 20.0 && point.frequency_hz <= 20000.0 &&
            point.gain_db >= -24.0 && point.gain_db <= 24.0) {
            continue;
        }
        if (!(point.frequency_hz == 0.0 && point.gain_db == 0.0 &&
              is_zero_tail(index + 1U))) {
            return false;
        }
    }

    std::size_t point_count = 0U;
    while (point_count < snapshot.points.size() &&
           !(snapshot.points[point_count].frequency_hz == 0.0 &&
             snapshot.points[point_count].gain_db == 0.0)) {
        ++point_count;
    }
    if (point_count < 4U) return false;
    write_u64(payload.data(), snapshot.sequence);
    payload[8U] = snapshot.source;
    payload[9U] = static_cast<std::uint8_t>(point_count);
    for (std::size_t index = 0U; index < point_count; ++index) {
        auto* bytes = payload.data() + kEqVisualSnapshotHeaderBytesV1 +
                      (index * kEqVisualSnapshotPointBytesV1);
        write_f64_bits(bytes, snapshot.points[index].frequency_hz);
        write_f64_bits(bytes + 8U, snapshot.points[index].gain_db);
    }
    payload_bytes = kEqVisualSnapshotHeaderBytesV1 +
                    (point_count * kEqVisualSnapshotPointBytesV1);
    return true;
}

bool decode_eq_visual_snapshot_v1(std::span<const std::uint8_t> payload,
                                  EqVisualSnapshotV1& snapshot) noexcept {
    snapshot = {};
    if (payload.size() < kEqVisualSnapshotHeaderBytesV1 ||
        payload.size() > kEqVisualSnapshotPayloadBytesV1 ||
        (payload.size() - kEqVisualSnapshotHeaderBytesV1) %
            kEqVisualSnapshotPointBytesV1 != 0U) {
        return false;
    }
    const auto sequence = read_u64(payload.data());
    const auto source = static_cast<std::uint8_t>(payload[8U]);
    const auto point_count = static_cast<std::size_t>(payload[9U]);
    const auto expected_bytes = kEqVisualSnapshotHeaderBytesV1 +
                                (point_count * kEqVisualSnapshotPointBytesV1);
    if (sequence == 0U || source == 0U || source > 2U || point_count < 4U ||
        payload.size() != expected_bytes) {
        return false;
    }
    double previous_frequency = 0.0;
    for (std::size_t index = 0U; index < point_count; ++index) {
        const auto* bytes = payload.data() + kEqVisualSnapshotHeaderBytesV1 +
                            (index * kEqVisualSnapshotPointBytesV1);
        const double frequency_hz = read_f64_bits(bytes);
        const double gain_db = read_f64_bits(bytes + 8U);
        if (!std::isfinite(frequency_hz) || !std::isfinite(gain_db) ||
            frequency_hz < 20.0 || frequency_hz > 20000.0 ||
            gain_db < -24.0 || gain_db > 24.0 ||
            frequency_hz <= previous_frequency) {
            return false;
        }
        previous_frequency = frequency_hz;
        snapshot.points[index] = EqVisualSnapshotPointV1{frequency_hz, gain_db};
    }
    snapshot.sequence = sequence;
    snapshot.source = source;
    return true;
}

bool EqVisualSnapshotStoreV1::publish(const EqVisualSnapshotV1& snapshot) noexcept {
    std::array<std::uint8_t, kEqVisualSnapshotPayloadBytesV1> candidate{};
    std::size_t candidate_bytes = 0U;
    if (!encode_eq_visual_snapshot_v1(snapshot, candidate, candidate_bytes)) return false;
    std::scoped_lock lock(mutex_);
    if (payload_bytes_ != 0U && snapshot.sequence <= sequence_) return false;
    payload_ = candidate;
    payload_bytes_ = candidate_bytes;
    sequence_ = snapshot.sequence;
    return true;
}

bool EqVisualSnapshotStoreV1::reply(IpcFrameV1& response) const noexcept {
    std::scoped_lock lock(mutex_);
    if (payload_bytes_ == 0U) return false;
    response = {};
    response.header.type = IpcMessageType::EqVisualSnapshot;
    response.header.payload_bytes = static_cast<std::uint32_t>(payload_bytes_);
    response.payload.assign(payload_.begin(), payload_.begin() +
                                           static_cast<std::ptrdiff_t>(payload_bytes_));
    return true;
}

bool EqVisualSnapshotStoreV1::has_snapshot() const noexcept {
    std::scoped_lock lock(mutex_);
    return payload_bytes_ != 0U;
}

std::uint64_t EqVisualSnapshotStoreV1::sequence() const noexcept {
    std::scoped_lock lock(mutex_);
    return sequence_;
}

bool eq_visual_snapshot_reply_v1(IpcFrameV1& response, void* const context) noexcept {
    auto* store = static_cast<EqVisualSnapshotStoreV1*>(context);
    return store != nullptr && store->reply(response);
}

bool is_printable_utf8_v1(const std::string_view value) noexcept {
    return is_printable_utf8(value);
}

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

std::array<std::uint8_t, kSessionVolumeCommandPayloadBytesV1>
encode_session_volume_command_v1(const SessionVolumeCommandV1& command) noexcept {
    std::array<std::uint8_t, kSessionVolumeCommandPayloadBytesV1> payload{};
    if (command.handle == 0U || command.catalog_sequence == 0U ||
        command.mute > 1U || command.requested_db_q16_16 < -144 * 65536 ||
        command.requested_db_q16_16 > 12 * 65536) {
        return payload;
    }
    write_u64(payload.data(), command.handle);
    write_u32(payload.data() + 8U,
              static_cast<std::uint32_t>(command.requested_db_q16_16));
    payload[12] = command.mute;
    write_u64(payload.data() + 16U, command.catalog_sequence);
    return payload;
}

bool decode_session_volume_command_v1(
    const std::span<const std::uint8_t> payload,
    SessionVolumeCommandV1& command) noexcept {
    command = {};
    if (payload.size() != kSessionVolumeCommandPayloadBytesV1 || payload[12] > 1U ||
        payload[13] != 0U || payload[14] != 0U || payload[15] != 0U) {
        return false;
    }
    const auto raw_db = static_cast<std::int32_t>(read_u32(payload.data() + 8U));
    const auto handle = read_u64(payload.data());
    const auto sequence = read_u64(payload.data() + 16U);
    if (handle == 0U || sequence == 0U || raw_db < -144 * 65536 ||
        raw_db > 12 * 65536) {
        return false;
    }
    command.handle = handle;
    command.requested_db_q16_16 = raw_db;
    command.mute = payload[12];
    command.catalog_sequence = sequence;
    return true;
}

std::array<std::uint8_t, kSessionRouteCommandPayloadBytesV1>
encode_session_route_command_v1(const SessionRouteCommandV1& command) noexcept {
    std::array<std::uint8_t, kSessionRouteCommandPayloadBytesV1> payload{};
    if (command.handle == 0U || command.catalog_sequence == 0U ||
        command.lane_bytes == 0U || command.lane_bytes > command.lane.size() ||
        command.output_group_bytes == 0U || command.output_group_bytes > command.output_group.size() ||
        !is_printable_utf8(std::string_view(command.lane.data(), command.lane_bytes)) ||
        !is_printable_utf8(std::string_view(command.output_group.data(),
                                             command.output_group_bytes))) {
        return payload;
    }
    write_u64(payload.data(), command.handle);
    write_u64(payload.data() + 8U, command.catalog_sequence);
    payload[16] = command.lane_bytes;
    payload[17] = command.output_group_bytes;
    std::copy_n(command.lane.data(), command.lane_bytes, payload.data() + 20U);
    std::copy_n(command.output_group.data(), command.output_group_bytes,
                payload.data() + 68U);
    return payload;
}

std::array<std::uint8_t, kIrPrepareCommandPayloadBytesV1>
encode_ir_prepare_command_v1(const IrPrepareCommandV1& command) noexcept {
    std::array<std::uint8_t, kIrPrepareCommandPayloadBytesV1> payload{};
    const auto valid_mode = command.mode <= 3U;
    const auto valid_strength = command.strength_q16_16 >= 0 &&
                                command.strength_q16_16 <= 65536;
    const auto valid_bypass = command.mode != 3U || command.strength_q16_16 == 0;
    const auto valid_rate = command.expected_sample_rate == 0U ||
                            (command.expected_sample_rate >= 8000U &&
                             command.expected_sample_rate <= 192000U);
    const auto valid_channels = command.expected_channels == 0U ||
                                (command.expected_channels >= 1U &&
                                 command.expected_channels <= 8U);
    if (command.schema_version != 1U || !valid_mode || !valid_strength || !valid_bypass ||
        !valid_rate || !valid_channels || command.path_bytes == 0U ||
        command.path_bytes > command.path.size() ||
        !is_printable_utf8(std::string_view(command.path.data(), command.path_bytes))) {
        return payload;
    }
    write_u32(payload.data(), command.schema_version);
    payload[4U] = command.mode;
    write_u32(payload.data() + 8U, static_cast<std::uint32_t>(command.strength_q16_16));
    write_u32(payload.data() + 12U, command.expected_sample_rate);
    write_u32(payload.data() + 16U, command.expected_channels);
    write_u16(payload.data() + 20U, command.path_bytes);
    std::copy_n(command.path.data(), command.path_bytes, payload.data() + 24U);
    return payload;
}

bool decode_ir_prepare_command_v1(const std::span<const std::uint8_t> payload,
                                  IrPrepareCommandV1& command) noexcept {
    command = {};
    if (payload.size() != kIrPrepareCommandPayloadBytesV1 || payload[5U] != 0U ||
        payload[6U] != 0U || payload[7U] != 0U || payload[22U] != 0U || payload[23U] != 0U) {
        return false;
    }
    for (std::size_t index = 284U; index < payload.size(); ++index) {
        if (payload[index] != 0U) return false;
    }
    const auto path_bytes = static_cast<std::size_t>(read_u16(payload.data() + 20U));
    const auto mode = payload[4U];
    const auto strength = static_cast<std::int32_t>(read_u32(payload.data() + 8U));
    const auto sample_rate = read_u32(payload.data() + 12U);
    const auto channels = read_u32(payload.data() + 16U);
    if (read_u32(payload.data()) != 1U || mode > 3U || strength < 0 || strength > 65536 ||
        (mode == 3U && strength != 0) ||
        (sample_rate != 0U && (sample_rate < 8000U || sample_rate > 192000U)) ||
        (channels != 0U && (channels < 1U || channels > 8U)) || path_bytes == 0U ||
        path_bytes > kIrPreparePathMaxBytesV1) {
        return false;
    }
    for (std::size_t index = path_bytes; index < kIrPreparePathMaxBytesV1; ++index) {
        if (payload[24U + index] != 0U) return false;
    }
    const std::string_view path(reinterpret_cast<const char*>(payload.data() + 24U), path_bytes);
    if (!is_printable_utf8(path)) return false;
    command.schema_version = 1U;
    command.mode = mode;
    command.strength_q16_16 = strength;
    command.expected_sample_rate = sample_rate;
    command.expected_channels = channels;
    command.path_bytes = static_cast<std::uint16_t>(path_bytes);
    std::copy_n(path.data(), path.size(), command.path.data());
    return true;
}

// ---- Scene catalog command (bounded v1 wire format) ----

namespace {

// Wire layout is little-endian and fully positional:
//   [0]         schema version (zero on the wire; decoded schema stays 1)
//   [1]         operation
//   [2..3]      reserved zero
//   [4..7]      latency mode
//   [8..11]     IR phase mode
//   [12..14]    auto attenuate / strict direct / graph output channels
//   [15..23]    lane count, timeline count and bounded-text lengths
//   [24..79]    finite f64 scene/loudness policies
//   [80..83]    loudness mode
//   [84]        loudness live update flag
//   [85..95]    reserved zero
//   [96..127]   scene ID
//   [128..247]  scene name
//   [248..311]  output group
//   [312..375]  optional IR reference
//   [376..439]  optional loudness anchor ID
//   [440..1463] NUL-terminated timeline IDs; unused entries stay zero
//   [1464...]   fixed-size lane records
constexpr std::size_t kSceneCatalogLaneWireBytesV1 = 448U;
constexpr std::size_t kSceneCatalogTimelineBaseV1 = 440U;
constexpr std::size_t kSceneCatalogLaneBaseV1 =
    kSceneCatalogTimelineBaseV1 +
    kSceneCatalogTimelineCapacityV1 * kSceneCatalogTimelineIdBytesV1;

void write_f64_bits(std::uint8_t* bytes, const double value) noexcept {
    std::uint64_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    write_u64(bytes, bits);
}

double read_f64_bits(const std::uint8_t* bytes) noexcept {
    const std::uint64_t bits = read_u64(bytes);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void write_f32_bits(std::uint8_t* bytes, const float value) noexcept {
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(bytes, bits);
}

float read_f32_bits(const std::uint8_t* bytes) noexcept {
    const std::uint32_t bits = read_u32(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace

bool encode_scene_catalog_command_v1(
    const SceneCatalogCommandV1& command,
    std::vector<std::uint8_t>& payload) noexcept {
    payload.clear();
    const auto raw_operation = static_cast<std::uint8_t>(command.operation);
    if (command.schema_version != 1U || raw_operation == 0U || raw_operation > 3U) {
        return false;
    }

    const auto text_is_valid = [](const auto& field, const std::size_t used,
                                  const bool required) noexcept {
        if (used > field.size() || (required && used == 0U)) return false;
        for (std::size_t index = used; index < field.size(); ++index) {
            if (field[index] != '\0') return false;
        }
        return used == 0U ||
               is_printable_utf8(std::string_view(field.data(), used));
    };

    if (command.operation == SessionRouteRuleOperationV1::Clear) {
        if (command.lane_count != 0U || command.timeline_count != 0U ||
            command.id_bytes != 0U || command.name_bytes != 0U ||
            command.output_group_bytes != 0U || command.ir_reference_bytes != 0U ||
            command.anchor_id_bytes != 0U) {
            return false;
        }
    } else if (command.operation == SessionRouteRuleOperationV1::Remove) {
        if (command.lane_count != 0U || command.timeline_count != 0U ||
            command.name_bytes != 0U || command.output_group_bytes != 0U ||
            command.ir_reference_bytes != 0U || command.anchor_id_bytes != 0U ||
            !text_is_valid(command.id, command.id_bytes, true)) {
            return false;
        }
    } else {
        if (command.lane_count == 0U ||
            command.lane_count > kSceneCatalogLaneCountV1 ||
            command.graph_output_channels != 2U &&
                command.graph_output_channels != 6U &&
                command.graph_output_channels != 8U ||
            command.auto_attenuate > 1U || command.strict_direct > 1U ||
            command.standard_id > 2U || command.calibrated_flag > 1U ||
            command.loudness_live_update > 1U ||
            command.timeline_count > kSceneCatalogTimelineCapacityV1 ||
            command.ir_reference_bytes > command.ir_reference.size() ||
            (command.ir_reference_bytes > 0U && command.ir_reference_bytes < 8U) ||
            command.anchor_id_bytes > command.anchor_id.size() ||
            !text_is_valid(command.id, command.id_bytes, true) ||
            !text_is_valid(command.name, command.name_bytes, true) ||
            !text_is_valid(command.output_group, command.output_group_bytes, true) ||
            (command.ir_reference_bytes != 0U &&
             !text_is_valid(command.ir_reference, command.ir_reference_bytes, true)) ||
            (command.anchor_id_bytes != 0U &&
             !text_is_valid(command.anchor_id, command.anchor_id_bytes, true))) {
            return false;
        }
        for (std::size_t item = 0U; item < kSceneCatalogTimelineCapacityV1; ++item) {
            const auto& source = command.timeline_ids[item];
            const auto terminator = std::find(source.begin(), source.end(), '\0');
            const auto length =
                static_cast<std::size_t>(terminator - source.begin());
            if ((item < command.timeline_count &&
                 (length == 0U || length >= kSceneCatalogTimelineIdBytesV1 ||
                  !is_printable_utf8(std::string_view(source.data(), length)))) ||
                (item >= command.timeline_count && length != 0U)) {
                return false;
            }
        }
        for (std::size_t lane_index = 0U; lane_index < command.lane_count;
             ++lane_index) {
            const auto& lane = command.lanes[lane_index];
            if (lane.enabled > 1U || lane.matrix_enabled > 1U ||
                lane.channel_count != 2U && lane.channel_count != 6U &&
                    lane.channel_count != 8U ||
                !text_is_valid(lane.id, lane.id_bytes, true) ||
                !text_is_valid(lane.output_group, lane.output_group_bytes, true)) {
                return false;
            }
        }
    }

    try {
        std::vector<std::uint8_t> bytes(kSceneCatalogCommandPayloadBytesV1, 0U);
        auto* p = bytes.data();
        p[1] = raw_operation;
        if (command.operation != SessionRouteRuleOperationV1::Clear) {
            std::copy_n(command.id.data(), command.id_bytes, p + 96U);
        }
        if (command.operation == SessionRouteRuleOperationV1::Upsert) {
            std::copy_n(command.name.data(), command.name_bytes, p + 128U);
            std::copy_n(command.output_group.data(), command.output_group_bytes,
                        p + 248U);
            if (command.ir_reference_bytes != 0U) {
                std::copy_n(command.ir_reference.data(), command.ir_reference_bytes,
                            p + 312U);
            }
            if (command.anchor_id_bytes != 0U) {
                std::copy_n(command.anchor_id.data(), command.anchor_id_bytes,
                            p + 376U);
            }
            write_u32(p + 4U, static_cast<std::uint32_t>(command.latency_mode));
            write_u32(p + 8U, static_cast<std::uint32_t>(command.ir_phase_mode));
            write_u32(p + 80U, static_cast<std::uint32_t>(command.loudness_mode));
            p[12] = command.auto_attenuate;
            p[13] = command.strict_direct;
            p[14] = command.graph_output_channels;
            write_f64_bits(p + 24U, command.limiter_dbtp);
            write_f64_bits(p + 32U, command.auto_attenuate_gain);
            write_f64_bits(p + 40U, command.reference_phon);
            write_f64_bits(p + 48U, command.strength);
            write_f64_bits(p + 56U, command.max_boost_db);
            write_f64_bits(p + 64U, command.measured_f3_hz);
            write_f64_bits(p + 72U, command.ir_phase_strength);
            p[84] = command.loudness_live_update;
            auto* timeline_base = p + kSceneCatalogTimelineBaseV1;
            for (std::size_t item = 0U; item < command.timeline_count; ++item) {
                const auto& source = command.timeline_ids[item];
                const auto terminator = std::find(source.begin(), source.end(), '\0');
                std::copy_n(source.data(),
                            static_cast<std::size_t>(terminator - source.begin()),
                            timeline_base + item * kSceneCatalogTimelineIdBytesV1);
            }
            auto* lane_base = p + kSceneCatalogLaneBaseV1;
            for (std::size_t lane_index = 0U; lane_index < command.lane_count;
                 ++lane_index) {
                const auto& lane = command.lanes[lane_index];
                auto* out = lane_base + lane_index * kSceneCatalogLaneWireBytesV1;
                std::copy_n(lane.id.data(), lane.id_bytes, out);
                std::copy_n(lane.output_group.data(), lane.output_group_bytes,
                            out + 31U);
                write_u32(out + 95U, lane.channel_count);
                write_u32(out + 99U,
                          static_cast<std::uint32_t>(db_to_q16_16(
                              static_cast<double>(lane.makeup_gain_db))));
                out[103] = lane.enabled;
                out[104] = lane.matrix_enabled;
                std::copy(lane.channel_map.begin(), lane.channel_map.end(),
                          out + 105U);
                write_u32(out + 113U, lane.reported_latency_samples);
                write_u16(out + 117U, static_cast<std::uint16_t>(lane.id_bytes));
                write_u16(out + 119U,
                          static_cast<std::uint16_t>(lane.output_group_bytes));
                auto* matrix_base = out + 135U;
                for (std::size_t row = 0U; row < 8U; ++row) {
                    for (std::size_t column = 0U; column < 8U; ++column) {
                        write_f32_bits(matrix_base + (row * 8U + column) * 4U,
                                       lane.channel_matrix[row][column]);
                    }
                }
            }
            p[15] = command.lane_count;
            p[16] = command.timeline_count;
            p[18] = command.name_bytes;
            p[19] = command.output_group_bytes;
            p[20] = command.ir_reference_bytes;
            p[21] = command.anchor_id_bytes;
            p[22] = command.standard_id;
            p[23] = command.calibrated_flag;
        }
        p[17] = command.id_bytes;
        payload.swap(bytes);
        return true;
    } catch (...) {
        payload.clear();
        return false;
    }
}

bool decode_scene_catalog_command_v1(
    const std::span<const std::uint8_t> payload,
    SceneCatalogCommandV1& command) noexcept {
    command = {};
    if (payload.size() != kSceneCatalogCommandPayloadBytesV1 || payload[0] != 0U ||
        payload[2] != 0U || payload[3] != 0U) {
        return false;
    }
    const auto raw_operation = payload[1];
    if (raw_operation == 0U || raw_operation > 3U) return false;
    const auto operation = static_cast<SessionRouteRuleOperationV1>(raw_operation);

    const auto check_text = [](const std::uint8_t* bytes, const std::size_t capacity,
                               const std::size_t used,
                               const bool allow_empty) noexcept {
        if (used > capacity || (!allow_empty && used == 0U)) return false;
        for (std::size_t index = used; index < capacity; ++index) {
            if (bytes[index] != 0U) return false;
        }
        return used == 0U ||
               is_printable_utf8(
                   std::string_view(reinterpret_cast<const char*>(bytes), used));
    };

    if (operation == SessionRouteRuleOperationV1::Clear) {
        for (std::size_t index = 4U; index < payload.size(); ++index) {
            if (payload[index] != 0U) return false;
        }
        command.operation = operation;
        return true;
    }

    const auto id_bytes = payload[17];
    if (!check_text(payload.data() + 96U, kSceneCatalogIdMaxBytesV1, id_bytes,
                    false)) {
        return false;
    }
    if (operation == SessionRouteRuleOperationV1::Remove) {
        for (std::size_t index = 24U; index < 96U; ++index) {
            if (payload[index] != 0U) return false;
        }
        if (payload[15] != 0U || payload[16] != 0U || payload[18] != 0U ||
            payload[19] != 0U || payload[20] != 0U || payload[21] != 0U ||
            payload[22] != 0U || payload[23] != 0U) {
            return false;
        }
        for (std::size_t index = 128U; index < payload.size(); ++index) {
            if (payload[index] != 0U) return false;
        }
        command.operation = operation;
        command.id_bytes = id_bytes;
        std::copy_n(payload.data() + 96U, id_bytes, command.id.data());
        return true;
    }

    // Upsert.
    if (payload[84] > 1U) return false;
    for (std::size_t index = 85U; index < 96U; ++index) {
        if (payload[index] != 0U) return false;
    }
    const auto latency_raw = read_u32(payload.data() + 4U);
    const auto ir_phase_raw = read_u32(payload.data() + 8U);
    const auto loudness_raw = read_u32(payload.data() + 80U);
    if (latency_raw > 3U || ir_phase_raw > 3U || loudness_raw > 2U) {
        return false;
    }
    command.latency_mode = static_cast<LatencyMode>(latency_raw);
    command.ir_phase_mode = static_cast<IrPhaseMode>(ir_phase_raw);
    command.loudness_mode = static_cast<EqualLoudnessMode>(loudness_raw);
    command.limiter_dbtp = read_f64_bits(payload.data() + 24U);
    command.auto_attenuate_gain = read_f64_bits(payload.data() + 32U);
    command.reference_phon = read_f64_bits(payload.data() + 40U);
    command.strength = read_f64_bits(payload.data() + 48U);
    command.max_boost_db = read_f64_bits(payload.data() + 56U);
    command.measured_f3_hz = read_f64_bits(payload.data() + 64U);
    command.ir_phase_strength = read_f64_bits(payload.data() + 72U);
    command.auto_attenuate = payload[12];
    command.strict_direct = payload[13];
    command.graph_output_channels = payload[14];
    command.lane_count = payload[15];
    command.timeline_count = payload[16];
    command.id_bytes = id_bytes;
    command.name_bytes = payload[18];
    command.output_group_bytes = payload[19];
    command.ir_reference_bytes = payload[20];
    command.anchor_id_bytes = payload[21];
    command.standard_id = payload[22];
    command.calibrated_flag = payload[23];
    command.loudness_live_update = payload[84];

    if (command.lane_count == 0U ||
        command.lane_count > kSceneCatalogLaneCountV1 ||
        command.timeline_count > kSceneCatalogTimelineCapacityV1 ||
        command.graph_output_channels != 2U &&
            command.graph_output_channels != 6U &&
            command.graph_output_channels != 8U ||
        command.auto_attenuate > 1U || command.strict_direct > 1U ||
        command.standard_id > 2U || command.calibrated_flag > 1U ||
        command.loudness_live_update > 1U ||
        command.ir_reference_bytes > command.ir_reference.size() ||
        (command.ir_reference_bytes > 0U && command.ir_reference_bytes < 8U) ||
        command.anchor_id_bytes > command.anchor_id.size() ||
        !std::isfinite(command.limiter_dbtp) ||
        !std::isfinite(command.auto_attenuate_gain) ||
        !std::isfinite(command.reference_phon) ||
        !std::isfinite(command.strength) ||
        !std::isfinite(command.max_boost_db) ||
        !std::isfinite(command.measured_f3_hz) ||
        !std::isfinite(command.ir_phase_strength) ||
        !check_text(payload.data() + 128U, kSceneCatalogNameMaxBytesV1,
                    command.name_bytes, false) ||
        !check_text(payload.data() + 248U, kSceneCatalogOutputGroupMaxBytesV1,
                    command.output_group_bytes, false) ||
        !check_text(payload.data() + 312U, command.ir_reference.size(),
                    command.ir_reference_bytes, true) ||
        !check_text(payload.data() + 376U, command.anchor_id.size(),
                    command.anchor_id_bytes, true)) {
        command = {};
        return false;
    }
    std::copy_n(payload.data() + 96U, id_bytes, command.id.data());
    std::copy_n(payload.data() + 128U, command.name_bytes, command.name.data());
    std::copy_n(payload.data() + 248U, command.output_group_bytes,
                command.output_group.data());
    if (command.ir_reference_bytes != 0U) {
        std::copy_n(payload.data() + 312U, command.ir_reference_bytes,
                    command.ir_reference.data());
    }
    if (command.anchor_id_bytes != 0U) {
        std::copy_n(payload.data() + 376U, command.anchor_id_bytes,
                    command.anchor_id.data());
    }

    const auto* timeline_base = payload.data() + kSceneCatalogTimelineBaseV1;
    for (std::size_t item = 0U; item < kSceneCatalogTimelineCapacityV1; ++item) {
        const auto* entry = timeline_base + item * kSceneCatalogTimelineIdBytesV1;
        std::size_t used = 0U;
        while (used < kSceneCatalogTimelineIdBytesV1 && entry[used] != 0U) ++used;
        if (used == kSceneCatalogTimelineIdBytesV1) { command = {}; return false; }
        if (item < command.timeline_count) {
            if (used == 0U ||
                !is_printable_utf8(
                    std::string_view(reinterpret_cast<const char*>(entry), used))) {
                command = {};
                return false;
            }
            std::copy_n(entry, used, command.timeline_ids[item].data());
        } else if (used != 0U) {
            command = {};
            return false;
        }
    }

    const auto* lane_base = payload.data() + kSceneCatalogLaneBaseV1;
    for (std::size_t lane_index = 0U; lane_index < command.lane_count; ++lane_index) {
        const auto* in = lane_base + lane_index * kSceneCatalogLaneWireBytesV1;
        auto& lane = command.lanes[lane_index];
        lane.id_bytes =
            static_cast<std::uint16_t>(in[117]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[118]) << 8U);
        lane.output_group_bytes =
            static_cast<std::uint16_t>(in[119]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[120]) << 8U);
        if (!check_text(in, kSceneCatalogIdMaxBytesV1, lane.id_bytes, false) ||
            !check_text(in + 31U, kSceneCatalogOutputGroupMaxBytesV1,
                        lane.output_group_bytes, false)) {
            command = {};
            return false;
        }
        std::copy_n(in, lane.id_bytes, lane.id.data());
        std::copy_n(in + 31U, lane.output_group_bytes, lane.output_group.data());
        lane.channel_count = read_u32(in + 95U);
        lane.makeup_gain_db = static_cast<float>(q16_16_to_db(
            static_cast<std::int32_t>(read_u32(in + 99U))));
        lane.enabled = in[103];
        lane.matrix_enabled = in[104];
        std::copy_n(in + 105U, lane.channel_map.size(), lane.channel_map.begin());
        lane.reported_latency_samples = read_u32(in + 113U);
        if (lane.channel_count != 2U && lane.channel_count != 6U &&
                lane.channel_count != 8U ||
            lane.enabled > 1U || lane.matrix_enabled > 1U) {
            command = {};
            return false;
        }
        const auto* matrix_base = in + 135U;
        for (std::size_t row = 0U; row < 8U; ++row) {
            for (std::size_t column = 0U; column < 8U; ++column) {
                lane.channel_matrix[row][column] = read_f32_bits(
                    matrix_base + (row * 8U + column) * 4U);
            }
        }
    }
    for (std::size_t lane_index = command.lane_count;
         lane_index < kSceneCatalogLaneCountV1; ++lane_index) {
        const auto* in = lane_base + lane_index * kSceneCatalogLaneWireBytesV1;
        for (std::size_t index = 0U; index < kSceneCatalogLaneWireBytesV1; ++index) {
            if (in[index] != 0U) { command = {}; return false; }
        }
    }
    return true;
}

bool decode_session_route_command_v1(
    const std::span<const std::uint8_t> payload,
    SessionRouteCommandV1& command) noexcept {
    command = {};
    if (payload.size() != kSessionRouteCommandPayloadBytesV1 || payload[16] == 0U ||
        payload[16] > command.lane.size() || payload[17] == 0U ||
        payload[17] > command.output_group.size() || payload[18] != 0U ||
        payload[19] != 0U) {
        return false;
    }
    const auto handle = read_u64(payload.data());
    const auto sequence = read_u64(payload.data() + 8U);
    if (handle == 0U || sequence == 0U) return false;
    for (std::size_t index = payload[16]; index < command.lane.size(); ++index) {
        if (payload[20U + index] != 0U) return false;
    }
    for (std::size_t index = payload[17]; index < command.output_group.size(); ++index) {
        if (payload[68U + index] != 0U) return false;
    }
    for (std::size_t index = 116U; index < payload.size(); ++index) {
        if (payload[index] != 0U) return false;
    }
    const std::string_view lane(reinterpret_cast<const char*>(payload.data() + 20U),
                                payload[16]);
    const std::string_view output(reinterpret_cast<const char*>(payload.data() + 68U),
                                  payload[17]);
    if (!is_printable_utf8(lane) || !is_printable_utf8(output)) return false;
    command.handle = handle;
    command.catalog_sequence = sequence;
    command.lane_bytes = payload[16];
    command.output_group_bytes = payload[17];
    std::copy_n(lane.data(), lane.size(), command.lane.data());
    std::copy_n(output.data(), output.size(), command.output_group.data());
    return true;
}

std::array<std::uint8_t, kSessionRouteRuleCommandPayloadBytesV1>
encode_session_route_rule_command_v1(
    const SessionRouteRuleCommandV1& command) noexcept {
    std::array<std::uint8_t, kSessionRouteRuleCommandPayloadBytesV1> payload{};
    const auto valid_operation =
        command.operation == SessionRouteRuleOperationV1::Upsert ||
        command.operation == SessionRouteRuleOperationV1::Remove ||
        command.operation == SessionRouteRuleOperationV1::Clear;
    const auto valid_owner = command.gain_owner == SessionRouteRuleGainOwnerV1::WindowsSession ||
                             command.gain_owner == SessionRouteRuleGainOwnerV1::HibikiInternal;
    const auto valid_text = [](const auto& text, const std::uint16_t bytes) noexcept {
        return bytes > 0U && bytes <= text.size() &&
               is_printable_utf8(std::string_view(text.data(), bytes));
    };
    if (command.schema_version != 1U || command.catalog_sequence == 0U ||
        !valid_operation || command.enabled > 1U || !valid_owner ||
        command.makeup_gain_q16_16 < (-144 * 65536) ||
        command.makeup_gain_q16_16 > (12 * 65536) ||
        command.rule_id_bytes > command.rule_id.size() ||
        command.app_id_bytes > command.app_id.size() ||
        command.display_name_bytes > command.display_name.size() ||
        command.lane_bytes > command.lane.size() ||
        command.output_group_bytes > command.output_group.size()) {
        return payload;
    }
    if (command.operation == SessionRouteRuleOperationV1::Upsert) {
        if (!valid_text(command.rule_id, command.rule_id_bytes) ||
            (!valid_text(command.app_id, command.app_id_bytes) &&
             !valid_text(command.display_name, command.display_name_bytes)) ||
            !valid_text(command.lane, command.lane_bytes) ||
            !valid_text(command.output_group, command.output_group_bytes)) {
            return payload;
        }
    } else if (command.operation == SessionRouteRuleOperationV1::Remove) {
        if (!valid_text(command.rule_id, command.rule_id_bytes) || command.app_id_bytes != 0U ||
            command.display_name_bytes != 0U || command.lane_bytes != 0U ||
            command.output_group_bytes != 0U) {
            return payload;
        }
    } else if (command.rule_id_bytes != 0U || command.app_id_bytes != 0U ||
               command.display_name_bytes != 0U || command.lane_bytes != 0U ||
               command.output_group_bytes != 0U) {
        return payload;
    }
    write_u32(payload.data(), command.schema_version);
    write_u32(payload.data() + 4U, static_cast<std::uint32_t>(command.priority));
    write_u32(payload.data() + 8U, static_cast<std::uint32_t>(command.makeup_gain_q16_16));
    payload[12U] = static_cast<std::uint8_t>(command.operation);
    payload[13U] = command.enabled;
    payload[14U] = static_cast<std::uint8_t>(command.gain_owner);
    write_u64(payload.data() + 16U, command.catalog_sequence);
    payload[24U] = static_cast<std::uint8_t>(command.rule_id_bytes);
    payload[25U] = static_cast<std::uint8_t>(command.app_id_bytes);
    payload[26U] = static_cast<std::uint8_t>(command.display_name_bytes);
    payload[27U] = static_cast<std::uint8_t>(command.lane_bytes);
    payload[28U] = static_cast<std::uint8_t>(command.output_group_bytes);
    std::copy_n(command.rule_id.data(), command.rule_id_bytes, payload.data() + 32U);
    std::copy_n(command.app_id.data(), command.app_id_bytes, payload.data() + 96U);
    std::copy_n(command.display_name.data(), command.display_name_bytes, payload.data() + 224U);
    std::copy_n(command.lane.data(), command.lane_bytes, payload.data() + 352U);
    std::copy_n(command.output_group.data(), command.output_group_bytes, payload.data() + 416U);
    return payload;
}

bool decode_session_route_rule_command_v1(
    const std::span<const std::uint8_t> payload,
    SessionRouteRuleCommandV1& command) noexcept {
    command = {};
    if (payload.size() != kSessionRouteRuleCommandPayloadBytesV1 ||
        payload[15U] != 0U || payload[29U] != 0U || payload[30U] != 0U ||
        payload[31U] != 0U) {
        return false;
    }
    const auto operation = static_cast<SessionRouteRuleOperationV1>(payload[12U]);
    const auto owner = static_cast<SessionRouteRuleGainOwnerV1>(payload[14U]);
    if (read_u32(payload.data()) != 1U ||
        (operation != SessionRouteRuleOperationV1::Upsert &&
         operation != SessionRouteRuleOperationV1::Remove &&
         operation != SessionRouteRuleOperationV1::Clear) ||
        payload[13U] > 1U ||
        (owner != SessionRouteRuleGainOwnerV1::WindowsSession &&
         owner != SessionRouteRuleGainOwnerV1::HibikiInternal) ||
        read_u64(payload.data() + 16U) == 0U || payload[24U] > kSessionRouteRuleIdMaxBytesV1 ||
        payload[25U] > kSessionRouteRuleMatchMaxBytesV1 ||
        payload[26U] > kSessionRouteRuleMatchMaxBytesV1 ||
        payload[27U] > kSessionRouteRuleRouteMaxBytesV1 ||
        payload[28U] > kSessionRouteRuleRouteMaxBytesV1) {
        return false;
    }
    const auto copy_text = [](const std::uint8_t* bytes, const std::size_t length,
                              auto& target) noexcept {
        for (std::size_t index = length; index < target.size(); ++index) {
            if (bytes[index] != 0U) return false;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes), length);
        if (length != 0U && !is_printable_utf8(text)) return false;
        std::copy_n(text.data(), text.size(), target.data());
        return true;
    };
    if (!copy_text(payload.data() + 32U, payload[24U], command.rule_id) ||
        !copy_text(payload.data() + 96U, payload[25U], command.app_id) ||
        !copy_text(payload.data() + 224U, payload[26U], command.display_name) ||
        !copy_text(payload.data() + 352U, payload[27U], command.lane) ||
        !copy_text(payload.data() + 416U, payload[28U], command.output_group)) {
        return false;
    }
    command.schema_version = 1U;
    command.priority = static_cast<std::int32_t>(read_u32(payload.data() + 4U));
    command.makeup_gain_q16_16 = static_cast<std::int32_t>(read_u32(payload.data() + 8U));
    if (command.makeup_gain_q16_16 < (-144 * 65536) ||
        command.makeup_gain_q16_16 > (12 * 65536)) {
        return false;
    }
    command.operation = operation;
    command.enabled = payload[13U];
    command.gain_owner = owner;
    command.catalog_sequence = read_u64(payload.data() + 16U);
    command.rule_id_bytes = payload[24U];
    command.app_id_bytes = payload[25U];
    command.display_name_bytes = payload[26U];
    command.lane_bytes = payload[27U];
    command.output_group_bytes = payload[28U];
    if (operation == SessionRouteRuleOperationV1::Upsert) {
        return command.rule_id_bytes != 0U &&
               (command.app_id_bytes != 0U || command.display_name_bytes != 0U) &&
               command.lane_bytes != 0U && command.output_group_bytes != 0U;
    }
    if (operation == SessionRouteRuleOperationV1::Remove) {
        return command.rule_id_bytes != 0U && command.app_id_bytes == 0U &&
               command.display_name_bytes == 0U && command.lane_bytes == 0U &&
               command.output_group_bytes == 0U;
    }
    return command.rule_id_bytes == 0U && command.app_id_bytes == 0U &&
           command.display_name_bytes == 0U && command.lane_bytes == 0U &&
           command.output_group_bytes == 0U;
}

// ---- Calibration PEQ prepare command (bounded v1 wire format) ----
// Layout is little-endian and fully positional:
//   [0..3]    schema version (u32 LE)
//   [4]       filter count (1..kCalibrationPeqMaxFiltersV1)
//   [5]       output group bytes (1..64)
//   [6]       clear existing flag (must be 0 when filter_count > 0)
//   [7..15]   reserved zero
//   [16..79]  NUL-padded printable UTF-8 output group
//   [80..463] filter entries, each 24 bytes:
//             frequency_hz (f64 LE), gain_db (f64 LE), q (f64 LE);
//             unused entries must stay all-zero

bool encode_calibration_peq_prepare_command_v1(
    const CalibrationPeqPrepareCommandV1& command,
    std::vector<std::uint8_t>& payload) noexcept {
    payload.clear();
    if (command.schema_version != 1U ||
        command.filter_count == 0U ||
        command.filter_count > kCalibrationPeqMaxFiltersV1 ||
        command.output_group_bytes == 0U ||
        command.output_group_bytes > command.output_group.size() ||
        command.clear_existing != 0U) {
        return false;
    }
    if (!is_printable_utf8(std::string_view(
            command.output_group.data(), command.output_group_bytes))) {
        return false;
    }
    const auto validate_filter = [](const auto& filter) noexcept {
        return std::isfinite(filter.frequency_hz) &&
               filter.frequency_hz >= 10.0 &&
               filter.frequency_hz <= 22000.0 &&
               std::isfinite(filter.gain_db) &&
               filter.gain_db >= -24.0 &&
               filter.gain_db <= 24.0 &&
               std::isfinite(filter.q) &&
               filter.q >= 0.05 &&
               filter.q <= 20.0;
    };
    for (std::size_t index = 0U; index < command.filter_count; ++index) {
        if (!validate_filter(command.filters[index])) return false;
    }
    for (std::size_t index = command.filter_count;
         index < kCalibrationPeqMaxFiltersV1; ++index) {
        const auto& entry = command.filters[index];
        if (entry.frequency_hz != 0.0 || entry.gain_db != 0.0 || entry.q != 0.0) {
            return false;
        }
    }
    payload.assign(kCalibrationPeqCommandPayloadBytesV1, std::uint8_t{0U});
    write_u32(payload.data(), command.schema_version);
    payload[4U] = command.filter_count;
    payload[5U] = static_cast<std::uint8_t>(command.output_group_bytes);
    payload[6U] = command.clear_existing;
    std::copy_n(command.output_group.data(), command.output_group_bytes,
                payload.data() + 16U);
    auto* base = payload.data() + 80U;
    for (std::size_t index = 0U; index < command.filter_count; ++index) {
        auto* offset = base + index * 24U;
        write_f64_bits(offset, command.filters[index].frequency_hz);
        write_f64_bits(offset + 8U, command.filters[index].gain_db);
        write_f64_bits(offset + 16U, command.filters[index].q);
    }
    return true;
}

bool decode_calibration_peq_prepare_command_v1(
    const std::span<const std::uint8_t> payload,
    CalibrationPeqPrepareCommandV1& command) noexcept {
    command = {};
    if (payload.size() != kCalibrationPeqCommandPayloadBytesV1) return false;
    for (std::size_t index = 7U; index < 16U; ++index) {
        if (payload[index] != 0U) return false;
    }
    const auto filter_count = payload[4U];
    const auto output_group_bytes = payload[5U];
    const auto clear_existing = payload[6U];
    if (filter_count == 0U || filter_count > kCalibrationPeqMaxFiltersV1 ||
        clear_existing != 0U || output_group_bytes == 0U ||
        output_group_bytes > kCalibrationPeqOutputGroupMaxBytesV1) {
        return false;
    }
    for (std::size_t index = output_group_bytes;
         index < kCalibrationPeqOutputGroupMaxBytesV1; ++index) {
        if (payload[16U + index] != 0U) return false;
    }
    const std::string_view output_group(
        reinterpret_cast<const char*>(payload.data() + 16U), output_group_bytes);
    if (!is_printable_utf8(output_group)) return false;
    for (std::size_t index = 0U; index < filter_count; ++index) {
        const auto* offset = payload.data() + 80U + index * 24U;
        const double freq = read_f64_bits(offset);
        const double gain = read_f64_bits(offset + 8U);
        const double q_value = read_f64_bits(offset + 16U);
        if (!std::isfinite(freq) || freq < 10.0 || freq > 22000.0 ||
            !std::isfinite(gain) || gain < -24.0 || gain > 24.0 ||
            !std::isfinite(q_value) || q_value < 0.05 || q_value > 20.0) {
            return false;
        }
        command.filters[index].frequency_hz = freq;
        command.filters[index].gain_db = gain;
        command.filters[index].q = q_value;
    }
    for (std::size_t index = filter_count;
         index < kCalibrationPeqMaxFiltersV1; ++index) {
        const auto* offset = payload.data() + 80U + index * 24U;
        if (read_f64_bits(offset) != 0.0 ||
            read_f64_bits(offset + 8U) != 0.0 ||
            read_f64_bits(offset + 16U) != 0.0) {
            return false;
        }
    }
    command.schema_version = read_u32(payload.data());
    command.filter_count = filter_count;
    command.output_group_bytes = output_group_bytes;
    command.clear_existing = clear_existing;
    std::copy_n(output_group.data(), output_group.size(), command.output_group.data());
    return true;
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

bool encode_device_catalog_snapshot_v1(
    const std::span<const DeviceCatalogSnapshotEntryV1> entries,
    const std::uint64_t catalog_sequence,
    std::array<std::uint8_t, kDeviceCatalogSnapshotPayloadBytesV1>& payload,
    std::size_t& payload_bytes) noexcept {
    payload.fill(0U);
    payload_bytes = 0U;
    if (entries.size() > kDeviceCatalogSnapshotCapacityV1) return false;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (entry.endpoint_id_bytes == 0U ||
            entry.endpoint_id_bytes > kDeviceSwitchEndpointMaxBytesV1 ||
            entry.display_name_bytes == 0U || entry.display_name_bytes > 128U ||
            (entry.flow != 0U && entry.flow != 1U) || entry.availability > 3U ||
            (entry.flags & static_cast<std::uint16_t>(~1U)) != 0U ||
            ((entry.flags & 1U) != 0U && entry.availability != 0U) ||
            !is_printable_utf8(std::string_view(entry.endpoint_id.data(),
                                                entry.endpoint_id_bytes)) ||
            !is_printable_utf8(std::string_view(entry.display_name.data(),
                                                entry.display_name_bytes)) ||
            (entry.channels != 1U && entry.channels != 2U && entry.channels != 6U &&
             entry.channels != 8U) ||
            (entry.sample_rate != 44100U && entry.sample_rate != 48000U &&
             entry.sample_rate != 96000U && entry.sample_rate != 192000U) ||
            entry.buffer_frames < 16U || entry.buffer_frames > 4096U) {
            return false;
        }
        const std::string_view endpoint(entry.endpoint_id.data(), entry.endpoint_id_bytes);
        for (std::size_t previous = 0U; previous < index; ++previous) {
            const auto& prior = entries[previous];
            if (endpoint == std::string_view(prior.endpoint_id.data(), prior.endpoint_id_bytes)) {
                return false;
            }
            if ((entry.flags & 1U) != 0U && (prior.flags & 1U) != 0U &&
                entry.flow == prior.flow) {
                return false;
            }
        }
    }
    write_u16(payload.data(), static_cast<std::uint16_t>(entries.size()));
    write_u64(payload.data() + 4U, catalog_sequence);
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        const auto offset = kDeviceCatalogSnapshotHeaderBytesV1 +
                             (index * kDeviceCatalogSnapshotEntryBytesV1);
        auto* bytes = payload.data() + offset;
        write_u16(bytes, entry.endpoint_id_bytes);
        write_u16(bytes + 2U, entry.display_name_bytes);
        bytes[4U] = entry.flow;
        bytes[5U] = entry.availability;
        write_u16(bytes + 6U, entry.flags);
        std::copy_n(entry.endpoint_id.data(), entry.endpoint_id_bytes,
                    bytes + 8U);
        std::copy_n(entry.display_name.data(), entry.display_name_bytes,
                    bytes + 268U);
        write_u32(bytes + 396U, entry.channels);
        write_u32(bytes + 400U, entry.sample_rate);
        write_u32(bytes + 404U, entry.buffer_frames);
        write_u64(bytes + 408U, entry.last_sequence);
    }
    payload_bytes = kDeviceCatalogSnapshotHeaderBytesV1 +
                    (entries.size() * kDeviceCatalogSnapshotEntryBytesV1);
    return true;
}

bool decode_device_catalog_snapshot_v1(
    const std::span<const std::uint8_t> payload,
    DeviceCatalogSnapshotV1& snapshot) noexcept {
    snapshot = {};
    if (payload.size() < kDeviceCatalogSnapshotHeaderBytesV1 ||
        payload.size() > kDeviceCatalogSnapshotPayloadBytesV1 || payload[2U] != 0U ||
        payload[3U] != 0U || payload[12U] != 0U || payload[13U] != 0U ||
        payload[14U] != 0U || payload[15U] != 0U) {
        return false;
    }
    const auto entry_count = static_cast<std::size_t>(read_u16(payload.data()));
    const auto expected_bytes = kDeviceCatalogSnapshotHeaderBytesV1 +
                                (entry_count * kDeviceCatalogSnapshotEntryBytesV1);
    if (entry_count > kDeviceCatalogSnapshotCapacityV1 || payload.size() != expected_bytes) {
        return false;
    }
    for (std::size_t index = 0U; index < entry_count; ++index) {
        const auto offset = kDeviceCatalogSnapshotHeaderBytesV1 +
                            (index * kDeviceCatalogSnapshotEntryBytesV1);
        const auto* bytes = payload.data() + offset;
        const auto endpoint_bytes = static_cast<std::size_t>(read_u16(bytes));
        const auto display_bytes = static_cast<std::size_t>(read_u16(bytes + 2U));
        const auto flow = bytes[4U];
        const auto availability = bytes[5U];
        const auto flags = read_u16(bytes + 6U);
        if (endpoint_bytes == 0U || endpoint_bytes > kDeviceSwitchEndpointMaxBytesV1 ||
            display_bytes == 0U || display_bytes > 128U || (flow != 0U && flow != 1U) ||
            availability > 3U || (flags & static_cast<std::uint16_t>(~1U)) != 0U ||
            ((flags & 1U) != 0U && availability != 0U)) {
            return false;
        }
        for (std::size_t pad = endpoint_bytes; pad < kDeviceSwitchEndpointMaxBytesV1; ++pad) {
            if (bytes[8U + pad] != 0U) return false;
        }
        for (std::size_t pad = display_bytes; pad < 128U; ++pad) {
            if (bytes[268U + pad] != 0U) return false;
        }
        const std::string_view endpoint(reinterpret_cast<const char*>(bytes + 8U), endpoint_bytes);
        const std::string_view display(reinterpret_cast<const char*>(bytes + 268U), display_bytes);
        const auto channels = read_u32(bytes + 396U);
        const auto sample_rate = read_u32(bytes + 400U);
        const auto buffer_frames = read_u32(bytes + 404U);
        if (!is_printable_utf8(endpoint) || !is_printable_utf8(display) ||
            (channels != 1U && channels != 2U && channels != 6U && channels != 8U) ||
            (sample_rate != 44100U && sample_rate != 48000U && sample_rate != 96000U &&
             sample_rate != 192000U) ||
            buffer_frames < 16U || buffer_frames > 4096U) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            const auto& prior = snapshot.entries[previous];
            if (endpoint == std::string_view(prior.endpoint_id.data(), prior.endpoint_id_bytes) ||
                ((flags & 1U) != 0U && (prior.flags & 1U) != 0U && flow == prior.flow)) {
                return false;
            }
        }
        auto& entry = snapshot.entries[index];
        entry.endpoint_id_bytes = static_cast<std::uint16_t>(endpoint_bytes);
        entry.display_name_bytes = static_cast<std::uint16_t>(display_bytes);
        std::copy_n(bytes + 8U, endpoint_bytes, entry.endpoint_id.data());
        std::copy_n(bytes + 268U, display_bytes, entry.display_name.data());
        entry.flow = flow;
        entry.availability = availability;
        entry.flags = flags;
        entry.channels = channels;
        entry.sample_rate = sample_rate;
        entry.buffer_frames = buffer_frames;
        entry.last_sequence = read_u64(bytes + 408U);
    }
    snapshot.entry_count = static_cast<std::uint16_t>(entry_count);
    snapshot.catalog_sequence = read_u64(payload.data() + 4U);
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
        case IpcMessageType::DeviceCatalogRequest:
        case IpcMessageType::ControlStatusRequest:
        case IpcMessageType::SessionCatalogRequest:
        case IpcMessageType::EqVisualSnapshotRequest:
            return frame.payload.empty();
        case IpcMessageType::VolumeNotification:
            if (frame.payload.size() == kVolumeNotificationPayloadBytesV1) {
                return decode_volume_notification_payload_v1(frame.payload, command.volume);
            }
            command.has_volume_target = decode_grouped_volume_notification_payload_v1(
                frame.payload, command.volume, command.volume_target);
            return command.has_volume_target;
        case IpcMessageType::SessionVolumeCommand:
            return decode_session_volume_command_v1(frame.payload, command.session_volume);
        case IpcMessageType::SessionRouteCommand:
            return decode_session_route_command_v1(frame.payload, command.session_route);
        case IpcMessageType::SessionRouteRuleCommand:
            return decode_session_route_rule_command_v1(frame.payload, command.session_route_rule);
        case IpcMessageType::IrPrepareCommand:
            return decode_ir_prepare_command_v1(frame.payload, command.ir_prepare);
        case IpcMessageType::SceneApply:
            return decode_scene_apply_payload_v1(frame.payload, command.scene);
        case IpcMessageType::SceneCatalogCommand:
            return decode_scene_catalog_command_v1(frame.payload, command.scene_catalog);
        case IpcMessageType::CalibrationPeqPrepare:
            return decode_calibration_peq_prepare_command_v1(
                frame.payload, command.calibration_peq);
        case IpcMessageType::DeviceSwitch:
            return decode_device_switch_payload_v1(frame.payload, command.device_switch);
        case IpcMessageType::GraphPrepare:
        case IpcMessageType::Ack:
        case IpcMessageType::Error:
        case IpcMessageType::ControlStatusSnapshot:
        case IpcMessageType::SessionCatalogSnapshot:
        case IpcMessageType::EqVisualSnapshot:
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

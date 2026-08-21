// SPDX-License-Identifier: Apache-2.0

#include "hibiki/driver_control_transport_v1.h"

#include <limits.h>
#include <string.h>

static uint16_t read_u16_le(const uint8_t* const bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t* const bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint64_t read_u64_le(const uint8_t* const bytes) {
    uint64_t value = 0U;
    size_t index;
    for (index = 0U; index < 8U; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8U);
    }
    return value;
}

static int32_t read_i32_le(const uint8_t* const bytes) {
    return (int32_t)read_u32_le(bytes);
}

static void write_u16_le(uint8_t* const bytes, const uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void write_u32_le(uint8_t* const bytes, const uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void write_u64_le(uint8_t* const bytes, const uint64_t value) {
    size_t index;
    for (index = 0U; index < 8U; ++index) {
        bytes[index] = (uint8_t)((value >> (index * 8U)) & 0xffU);
    }
}

static void write_i32_le(uint8_t* const bytes, const int32_t value) {
    write_u32_le(bytes, (uint32_t)value);
}

static int valid_guid(const uint8_t* const guid) {
    return guid != NULL &&
           memchr(guid, '\0', HIBIKI_ENDPOINT_GUID_CAPACITY) != NULL;
}

static int valid_channels(const uint32_t channels) {
    return channels == 2U || channels == 6U || channels == 8U;
}

static int valid_rate(const uint32_t rate) {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

static int valid_db_q16_16(const int32_t value) {
    return value >= (-144 * 65536) && value <= (12 * 65536);
}

static int valid_message_type(const uint16_t message_type) {
    return message_type == HIBIKI_DRIVER_ENDPOINT_STATE ||
           message_type == HIBIKI_DRIVER_VOLUME_NOTIFICATION;
}

static int valid_packet_header(const uint8_t* const packet,
                               const size_t packet_bytes) {
    if (packet == NULL || packet_bytes != HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1 ||
        read_u32_le(packet) != HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1 ||
        read_u16_le(packet + 4U) != HIBIKI_DRIVER_CONTROL_ABI_V1 ||
        !valid_message_type(read_u16_le(packet + 6U)) ||
        !valid_guid(packet + HIBIKI_DRIVER_CONTROL_ENDPOINT_GUID_OFFSET_V1) ||
        !valid_guid(packet + HIBIKI_DRIVER_CONTROL_EVENT_CONTEXT_GUID_OFFSET_V1)) {
        return 0;
    }
    return 1;
}

int hibiki_driver_endpoint_state_packet_validate_v1(
    const uint8_t* const packet,
    const size_t packet_bytes) {
    if (!valid_packet_header(packet, packet_bytes)) return 0;
    const uint32_t channels = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_CHANNELS_OFFSET_V1);
    const uint32_t sample_rate =
        read_u32_le(packet + HIBIKI_DRIVER_CONTROL_SAMPLE_RATE_OFFSET_V1);
    const uint32_t frames = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_FRAMES_OFFSET_V1);
    const int32_t requested =
        read_i32_le(packet + HIBIKI_DRIVER_CONTROL_REQUESTED_DB_OFFSET_V1);
    const int32_t safety = read_i32_le(packet + HIBIKI_DRIVER_CONTROL_SAFETY_DB_OFFSET_V1);
    const int32_t effective =
        read_i32_le(packet + HIBIKI_DRIVER_CONTROL_EFFECTIVE_DB_OFFSET_V1);
    const uint32_t mute = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_MUTE_OFFSET_V1);
    const uint64_t generation =
        read_u64_le(packet + HIBIKI_DRIVER_CONTROL_GENERATION_OFFSET_V1);
    const uint32_t actuator = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_ACTUATOR_OFFSET_V1);
    return valid_channels(channels) && valid_rate(sample_rate) && frames != 0U &&
           frames <= 4096U && valid_db_q16_16(requested) && valid_db_q16_16(safety) &&
           valid_db_q16_16(effective) && mute <= 1U && generation != 0U &&
           actuator <= HIBIKI_ACTUATOR_STRICT_DIRECT;
}

int hibiki_driver_endpoint_state_packet_encode_v1(
    uint8_t* const packet,
    const size_t packet_capacity,
    const uint16_t message_type,
    const uint64_t request_id,
    const struct hibiki_driver_endpoint_state_v1* const state,
    size_t* const written_bytes) {
    if (written_bytes != NULL) *written_bytes = 0U;
    if (packet == NULL || packet_capacity < HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1 ||
        written_bytes == NULL || state == NULL || !valid_message_type(message_type) ||
        !valid_guid((const uint8_t*)state->endpoint_guid) ||
        !valid_guid((const uint8_t*)state->event_context_guid) ||
        !valid_channels(state->channel_count) || !valid_rate(state->sample_rate) ||
        state->frames_per_buffer == 0U || state->frames_per_buffer > 4096U ||
        !valid_db_q16_16(state->requested_db_q16_16) ||
        !valid_db_q16_16(state->safety_ceiling_db_q16_16) ||
        !valid_db_q16_16(state->effective_db_q16_16) || state->mute > 1U ||
        state->generation == 0U || state->actuator > HIBIKI_ACTUATOR_STRICT_DIRECT) {
        return 0;
    }
    memset(packet, 0, HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1);
    write_u32_le(packet, HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1);
    write_u16_le(packet + 4U, HIBIKI_DRIVER_CONTROL_ABI_V1);
    write_u16_le(packet + 6U, message_type);
    write_u64_le(packet + 8U, request_id);
    memcpy(packet + HIBIKI_DRIVER_CONTROL_ENDPOINT_GUID_OFFSET_V1,
           state->endpoint_guid, HIBIKI_ENDPOINT_GUID_CAPACITY);
    memcpy(packet + HIBIKI_DRIVER_CONTROL_EVENT_CONTEXT_GUID_OFFSET_V1,
           state->event_context_guid, HIBIKI_ENDPOINT_GUID_CAPACITY);
    write_u32_le(packet + HIBIKI_DRIVER_CONTROL_CHANNELS_OFFSET_V1, state->channel_count);
    write_u32_le(packet + HIBIKI_DRIVER_CONTROL_SAMPLE_RATE_OFFSET_V1, state->sample_rate);
    write_u32_le(packet + HIBIKI_DRIVER_CONTROL_FRAMES_OFFSET_V1, state->frames_per_buffer);
    write_i32_le(packet + HIBIKI_DRIVER_CONTROL_REQUESTED_DB_OFFSET_V1,
                 state->requested_db_q16_16);
    write_i32_le(packet + HIBIKI_DRIVER_CONTROL_SAFETY_DB_OFFSET_V1,
                 state->safety_ceiling_db_q16_16);
    write_i32_le(packet + HIBIKI_DRIVER_CONTROL_EFFECTIVE_DB_OFFSET_V1,
                 state->effective_db_q16_16);
    write_u32_le(packet + HIBIKI_DRIVER_CONTROL_MUTE_OFFSET_V1, state->mute);
    write_u64_le(packet + HIBIKI_DRIVER_CONTROL_GENERATION_OFFSET_V1, state->generation);
    write_u32_le(packet + HIBIKI_DRIVER_CONTROL_ACTUATOR_OFFSET_V1, state->actuator);
    *written_bytes = HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1;
    return 1;
}

int hibiki_driver_endpoint_state_packet_decode_v1(
    const uint8_t* const packet,
    const size_t packet_bytes,
    struct hibiki_driver_endpoint_state_v1* const state,
    uint16_t* const message_type,
    uint64_t* const request_id) {
    if (state == NULL || message_type == NULL || request_id == NULL ||
        !hibiki_driver_endpoint_state_packet_validate_v1(packet, packet_bytes)) {
        return 0;
    }
    memset(state, 0, sizeof(*state));
    state->header.size_bytes = (uint32_t)sizeof(*state);
    state->header.abi_version = HIBIKI_DRIVER_CONTROL_ABI_V1;
    state->header.message_type = read_u16_le(packet + 6U);
    state->header.request_id = read_u64_le(packet + 8U);
    memcpy(state->endpoint_guid,
           packet + HIBIKI_DRIVER_CONTROL_ENDPOINT_GUID_OFFSET_V1,
           HIBIKI_ENDPOINT_GUID_CAPACITY);
    memcpy(state->event_context_guid,
           packet + HIBIKI_DRIVER_CONTROL_EVENT_CONTEXT_GUID_OFFSET_V1,
           HIBIKI_ENDPOINT_GUID_CAPACITY);
    state->channel_count = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_CHANNELS_OFFSET_V1);
    state->sample_rate = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_SAMPLE_RATE_OFFSET_V1);
    state->frames_per_buffer = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_FRAMES_OFFSET_V1);
    state->requested_db_q16_16 =
        read_i32_le(packet + HIBIKI_DRIVER_CONTROL_REQUESTED_DB_OFFSET_V1);
    state->safety_ceiling_db_q16_16 =
        read_i32_le(packet + HIBIKI_DRIVER_CONTROL_SAFETY_DB_OFFSET_V1);
    state->effective_db_q16_16 =
        read_i32_le(packet + HIBIKI_DRIVER_CONTROL_EFFECTIVE_DB_OFFSET_V1);
    state->mute = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_MUTE_OFFSET_V1);
    state->generation = read_u64_le(packet + HIBIKI_DRIVER_CONTROL_GENERATION_OFFSET_V1);
    state->actuator = read_u32_le(packet + HIBIKI_DRIVER_CONTROL_ACTUATOR_OFFSET_V1);
    *message_type = state->header.message_type;
    *request_id = state->header.request_id;
    return 1;
}

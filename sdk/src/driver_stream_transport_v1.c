// SPDX-License-Identifier: Apache-2.0

#include "hibiki/driver_stream_transport_v1.h"

#include <math.h>
#include <string.h>

static int valid_channels(const uint32_t channels) {
    return channels == 2U || channels == 6U || channels == 8U;
}

static int valid_rate(const uint32_t rate) {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

static int valid_guid(const char* const guid) {
    return guid != NULL && memchr(guid, '\0', HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1) != NULL;
}

static void copy_guid(char* const destination, const char* const source) {
    memset(destination, 0, HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1);
    for (size_t index = 0U; index + 1U < HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1;
         ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') return;
    }
    destination[HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1 - 1U] = '\0';
}

static int valid_flags(const uint32_t flags) {
    return (flags & ~(HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1 |
                      HIBIKI_DRIVER_STREAM_FLAG_SILENCE_V1)) == 0U;
}

static int finite_samples(const uint8_t* const samples, const size_t sample_count) {
    for (size_t index = 0U; index < sample_count; ++index) {
        float value;
        memcpy(&value, samples + index * sizeof(value), sizeof(value));
        if (!isfinite(value)) return 0;
    }
    return 1;
}

static int valid_header(const struct hibiki_driver_stream_packet_header_v1* const header,
                        const size_t packet_bytes) {
    if (header == NULL || packet_bytes < HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 ||
        header->size_bytes != packet_bytes ||
        header->abi_version != HIBIKI_DRIVER_STREAM_TRANSPORT_ABI_V1 ||
        (header->packet_type != HIBIKI_DRIVER_STREAM_RENDER_V1 &&
         header->packet_type != HIBIKI_DRIVER_STREAM_CAPTURE_V1) ||
        header->sequence == 0U || header->generation == 0U ||
        !valid_guid(header->endpoint_guid) || !valid_channels(header->channels) ||
        !valid_rate(header->sample_rate) || header->frames == 0U ||
        header->frames > HIBIKI_DRIVER_STREAM_MAX_FRAMES_V1 || !valid_flags(header->flags)) {
        return 0;
    }
    const size_t sample_count = (size_t)header->frames * header->channels;
    const size_t sample_bytes = sample_count * sizeof(float);
    if (sample_bytes > SIZE_MAX - HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 ||
        HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 + sample_bytes != packet_bytes) {
        return 0;
    }
    return 1;
}

int hibiki_driver_stream_packet_validate_v1(const uint8_t* const packet,
                                            const size_t packet_bytes) {
    struct hibiki_driver_stream_packet_header_v1 header;
    if (packet == NULL || packet_bytes < sizeof(header)) return 0;
    memcpy(&header, packet, sizeof(header));
    return valid_header(&header, packet_bytes);
}

int hibiki_driver_stream_packet_encode_v1(
    uint8_t* const packet,
    const size_t packet_capacity,
    const uint16_t packet_type,
    const uint64_t sequence,
    const char* const endpoint_guid,
    const uint32_t channels,
    const uint32_t sample_rate,
    const uint32_t frames,
    const uint32_t flags,
    const uint64_t generation,
    const float* const interleaved_samples,
    size_t* const written_bytes) {
    if (written_bytes != NULL) *written_bytes = 0U;
    if (packet == NULL || written_bytes == NULL || interleaved_samples == NULL ||
        !valid_guid(endpoint_guid) || !valid_channels(channels) || !valid_rate(sample_rate) ||
        frames == 0U || frames > HIBIKI_DRIVER_STREAM_MAX_FRAMES_V1 || !valid_flags(flags) ||
        sequence == 0U || generation == 0U ||
        (packet_type != HIBIKI_DRIVER_STREAM_RENDER_V1 &&
         packet_type != HIBIKI_DRIVER_STREAM_CAPTURE_V1)) {
        return 0;
    }
    const size_t sample_bytes = (size_t)frames * channels * sizeof(float);
    if (sample_bytes > SIZE_MAX - HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1) return 0;
    const size_t total_bytes = HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 + sample_bytes;
    if (packet_capacity < total_bytes || total_bytes > UINT32_MAX) return 0;
    if (!finite_samples((const uint8_t*)interleaved_samples,
                        (size_t)frames * channels)) return 0;
    struct hibiki_driver_stream_packet_header_v1 header;
    memset(&header, 0, sizeof(header));
    header.size_bytes = (uint32_t)total_bytes;
    header.abi_version = HIBIKI_DRIVER_STREAM_TRANSPORT_ABI_V1;
    header.packet_type = packet_type;
    header.sequence = sequence;
    copy_guid(header.endpoint_guid, endpoint_guid);
    header.channels = channels;
    header.sample_rate = sample_rate;
    header.frames = frames;
    header.flags = flags;
    header.generation = generation;
    memcpy(packet, &header, sizeof(header));
    memcpy(packet + sizeof(header), interleaved_samples, sample_bytes);
    *written_bytes = total_bytes;
    return 1;
}

int hibiki_driver_stream_packet_payload_v1(
    const uint8_t* const packet,
    const size_t packet_bytes,
    const uint8_t** const payload,
    size_t* const payload_bytes) {
    if (payload == NULL || payload_bytes == NULL ||
        !hibiki_driver_stream_packet_validate_v1(packet, packet_bytes)) {
        return 0;
    }
    *payload = packet + HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1;
    *payload_bytes = packet_bytes - HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1;
    return 1;
}

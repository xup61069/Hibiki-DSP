#ifndef HIBIKI_DRIVER_STREAM_TRANSPORT_V1_H
#define HIBIKI_DRIVER_STREAM_TRANSPORT_V1_H

// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>
#include <stdint.h>

#define HIBIKI_DRIVER_STREAM_TRANSPORT_ABI_V1 1u
#define HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1 40u
#define HIBIKI_DRIVER_STREAM_MAX_FRAMES_V1 4096u
#define HIBIKI_DRIVER_STREAM_MAX_CHANNELS_V1 8u
#define HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 80u

enum hibiki_driver_stream_packet_type_v1 {
    HIBIKI_DRIVER_STREAM_RENDER_V1 = 1u,
    HIBIKI_DRIVER_STREAM_CAPTURE_V1 = 2u
};

enum hibiki_driver_stream_flags_v1 {
    HIBIKI_DRIVER_STREAM_FLAG_DISCONTINUITY_V1 = 1u,
    HIBIKI_DRIVER_STREAM_FLAG_SILENCE_V1 = 2u
};

// Native Windows little-endian wire layout. The packet header is followed by
// `frames * channels * sizeof(float)` interleaved Float32 bytes.
struct hibiki_driver_stream_packet_header_v1 {
    uint32_t size_bytes;
    uint16_t abi_version;
    uint16_t packet_type;
    uint64_t sequence;
    char endpoint_guid[HIBIKI_DRIVER_STREAM_ENDPOINT_GUID_CAPACITY_V1];
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t frames;
    uint32_t flags;
    uint64_t generation;
};

#if defined(__cplusplus)
static_assert(sizeof(struct hibiki_driver_stream_packet_header_v1) ==
                  HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1,
              "driver stream packet header ABI changed");
extern "C" {
#endif

int hibiki_driver_stream_packet_validate_v1(const uint8_t* packet, size_t packet_bytes);

int hibiki_driver_stream_packet_encode_v1(
    uint8_t* packet,
    size_t packet_capacity,
    uint16_t packet_type,
    uint64_t sequence,
    const char* endpoint_guid,
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t frames,
    uint32_t flags,
    uint64_t generation,
    const float* interleaved_samples,
    size_t* written_bytes);

int hibiki_driver_stream_packet_payload_v1(
    const uint8_t* packet,
    size_t packet_bytes,
    const uint8_t** payload,
    size_t* payload_bytes);

#if defined(__cplusplus)
}
#endif

#endif

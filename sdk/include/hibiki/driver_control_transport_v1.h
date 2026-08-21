#ifndef HIBIKI_DRIVER_CONTROL_TRANSPORT_V1_H
#define HIBIKI_DRIVER_CONTROL_TRANSPORT_V1_H

// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>
#include <stdint.h>

#include "hibiki/driver_control_v1.h"

#define HIBIKI_DRIVER_CONTROL_HEADER_BYTES_V1 16u
#define HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1 136u

// The transport is an explicit little-endian byte contract.  It must not
// memcpy a C struct across the driver/user-space boundary because compiler
// padding and alignment are not part of the ABI.
#define HIBIKI_DRIVER_CONTROL_ENDPOINT_GUID_OFFSET_V1 16u
#define HIBIKI_DRIVER_CONTROL_EVENT_CONTEXT_GUID_OFFSET_V1 56u
#define HIBIKI_DRIVER_CONTROL_CHANNELS_OFFSET_V1 96u
#define HIBIKI_DRIVER_CONTROL_SAMPLE_RATE_OFFSET_V1 100u
#define HIBIKI_DRIVER_CONTROL_FRAMES_OFFSET_V1 104u
#define HIBIKI_DRIVER_CONTROL_REQUESTED_DB_OFFSET_V1 108u
#define HIBIKI_DRIVER_CONTROL_SAFETY_DB_OFFSET_V1 112u
#define HIBIKI_DRIVER_CONTROL_EFFECTIVE_DB_OFFSET_V1 116u
#define HIBIKI_DRIVER_CONTROL_MUTE_OFFSET_V1 120u
#define HIBIKI_DRIVER_CONTROL_GENERATION_OFFSET_V1 124u
#define HIBIKI_DRIVER_CONTROL_ACTUATOR_OFFSET_V1 132u

#if defined(__cplusplus)
static_assert(HIBIKI_DRIVER_CONTROL_HEADER_BYTES_V1 == 16u,
              "driver control header byte contract changed");
static_assert(HIBIKI_DRIVER_CONTROL_ENDPOINT_STATE_PACKET_BYTES_V1 == 136u,
              "driver control endpoint packet byte contract changed");
extern "C" {
#endif

// Validates one complete endpoint-state or volume-notification packet.  The
// span must be exactly the encoded size; larger ring slots are not accepted.
int hibiki_driver_endpoint_state_packet_validate_v1(
    const uint8_t* packet,
    size_t packet_bytes);

// Encodes the public state into explicit little-endian bytes.  `message_type`
// must be HIBIKI_DRIVER_ENDPOINT_STATE or HIBIKI_DRIVER_VOLUME_NOTIFICATION.
// The source struct's C padding is ignored.
int hibiki_driver_endpoint_state_packet_encode_v1(
    uint8_t* packet,
    size_t packet_capacity,
    uint16_t message_type,
    uint64_t request_id,
    const struct hibiki_driver_endpoint_state_v1* state,
    size_t* written_bytes);

// Decodes a validated packet into caller-owned state.  On success the output
// struct has a host-local header and can be passed to the existing MS-PL
// validator; no pointer into `packet` is retained.
int hibiki_driver_endpoint_state_packet_decode_v1(
    const uint8_t* packet,
    size_t packet_bytes,
    struct hibiki_driver_endpoint_state_v1* state,
    uint16_t* message_type,
    uint64_t* request_id);

#if defined(__cplusplus)
}
#endif

#endif

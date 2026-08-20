#ifndef HIBIKI_DRIVER_CONTROL_V1_H
#define HIBIKI_DRIVER_CONTROL_V1_H

// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>

#define HIBIKI_DRIVER_CONTROL_ABI_V1 1u
#define HIBIKI_ENDPOINT_GUID_CAPACITY 40u

enum hibiki_driver_message_type_v1 {
    HIBIKI_DRIVER_HELLO = 1,
    HIBIKI_DRIVER_VOLUME_NOTIFICATION = 2,
    HIBIKI_DRIVER_ENDPOINT_STATE = 3,
    HIBIKI_DRIVER_ACK = 4,
    HIBIKI_DRIVER_ERROR = 5
};

enum hibiki_driver_actuator_v1 {
    HIBIKI_ACTUATOR_INTERNAL_DSP = 0,
    HIBIKI_ACTUATOR_DEVICE_HARDWARE = 1,
    HIBIKI_ACTUATOR_STRICT_DIRECT = 2
};

struct hibiki_driver_message_header_v1 {
    uint32_t size_bytes;
    uint16_t abi_version;
    uint16_t message_type;
    uint64_t request_id;
};

struct hibiki_driver_endpoint_state_v1 {
    struct hibiki_driver_message_header_v1 header;
    char endpoint_guid[HIBIKI_ENDPOINT_GUID_CAPACITY];
    char event_context_guid[HIBIKI_ENDPOINT_GUID_CAPACITY];
    uint32_t channel_count;
    uint32_t sample_rate;
    uint32_t frames_per_buffer;
    int32_t requested_db_q16_16;
    int32_t safety_ceiling_db_q16_16;
    int32_t effective_db_q16_16;
    uint32_t mute;
    uint64_t generation;
    uint32_t actuator;
};

#if defined(__cplusplus)
static_assert(sizeof(struct hibiki_driver_message_header_v1) == 16,
              "driver control header ABI changed");
#endif

#endif

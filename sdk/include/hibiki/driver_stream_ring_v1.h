#ifndef HIBIKI_DRIVER_STREAM_RING_V1_H
#define HIBIKI_DRIVER_STREAM_RING_V1_H

// SPDX-License-Identifier: Apache-2.0

#include "hibiki/driver_stream_transport_v1.h"

#include <stddef.h>
#include <stdint.h>

#define HIBIKI_DRIVER_STREAM_RING_ABI_V1 1u
#define HIBIKI_DRIVER_STREAM_RING_MAGIC_V1 0x48445352u /* HDSR */
#define HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1 4u
#define HIBIKI_DRIVER_STREAM_RING_RESERVED_COUNT_V1 4u
#define HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1 \
    (HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 + \
     ((uint32_t)HIBIKI_DRIVER_STREAM_MAX_CHANNELS_V1 * \
      (uint32_t)HIBIKI_DRIVER_STREAM_MAX_FRAMES_V1 * (uint32_t)sizeof(float)))

enum hibiki_driver_stream_ring_result_v1 {
    HIBIKI_DRIVER_STREAM_RING_OK_V1 = 0,
    HIBIKI_DRIVER_STREAM_RING_UNDERRUN_V1 = 1,
    HIBIKI_DRIVER_STREAM_RING_REJECTED_V1 = -1,
    HIBIKI_DRIVER_STREAM_RING_OVERRUN_V1 = -2,
    HIBIKI_DRIVER_STREAM_RING_INVALID_V1 = -3
};

struct hibiki_driver_stream_ring_slot_v1 {
    volatile uint32_t ready_sequence;
    uint32_t packet_bytes;
    uint32_t reserved[2];
    uint8_t packet[HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1];
};

struct hibiki_driver_stream_ring_v1 {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t slot_count;
    uint32_t slot_capacity_bytes;
    volatile uint32_t producer_sequence;
    volatile uint32_t consumer_sequence;
    volatile uint32_t overrun_count;
    volatile uint32_t underrun_count;
    uint32_t reserved[HIBIKI_DRIVER_STREAM_RING_RESERVED_COUNT_V1];
    struct hibiki_driver_stream_ring_slot_v1 slots[HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1];
};

#if defined(__cplusplus)
static_assert(sizeof(struct hibiki_driver_stream_ring_v1) <= (size_t)1U << 20U,
              "driver stream ring region must remain bounded for shared memory");
extern "C" {
#endif

size_t hibiki_driver_stream_ring_region_size_v1(void);

int hibiki_driver_stream_ring_init_v1(
    struct hibiki_driver_stream_ring_v1* ring,
    size_t region_bytes,
    uint32_t channels,
    uint32_t sample_rate);

int hibiki_driver_stream_ring_push_v1(
    struct hibiki_driver_stream_ring_v1* ring,
    size_t region_bytes,
    const uint8_t* packet,
    size_t packet_bytes);

int hibiki_driver_stream_ring_pop_v1(
    struct hibiki_driver_stream_ring_v1* ring,
    size_t region_bytes,
    uint8_t* packet_storage,
    size_t storage_capacity,
    size_t* packet_bytes,
    uint32_t* silence);

void hibiki_driver_stream_ring_reset_v1(struct hibiki_driver_stream_ring_v1* ring);

#if defined(__cplusplus)
}
#endif

#endif

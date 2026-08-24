// SPDX-License-Identifier: Apache-2.0

#include "hibiki/driver_stream_ring_v1.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>

static uint32_t load_acquire(const volatile uint32_t* value) {
    return (uint32_t)InterlockedCompareExchange((volatile LONG*)value, 0, 0);
}

static void store_release(volatile uint32_t* value, const uint32_t next) {
    InterlockedExchange((volatile LONG*)value, (LONG)next);
}

static void increment_relaxed(volatile uint32_t* value) {
    InterlockedIncrement((volatile LONG*)value);
}
#else
static uint32_t load_acquire(const volatile uint32_t* value) {
    return (uint32_t)__atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_release(volatile uint32_t* value, const uint32_t next) {
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static void increment_relaxed(volatile uint32_t* value) {
    (void)__atomic_add_fetch(value, 1U, __ATOMIC_RELAXED);
}
#endif

static int valid_channels(const uint32_t channels) {
    return channels == 2U || channels == 6U || channels == 8U;
}

static int valid_sample_rate(const uint32_t sample_rate) {
    return sample_rate == 44100U || sample_rate == 48000U ||
           sample_rate == 96000U || sample_rate == 192000U;
}

static int valid_ring(const struct hibiki_driver_stream_ring_v1* const ring,
                      const size_t region_bytes,
                      const int check_reserved) {
    if (ring == NULL || region_bytes < sizeof(*ring) ||
        ring->magic != HIBIKI_DRIVER_STREAM_RING_MAGIC_V1 ||
        ring->abi_version != HIBIKI_DRIVER_STREAM_RING_ABI_V1 ||
        ring->slot_count != HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1 ||
        ring->slot_capacity_bytes !=
            HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1 ||
        !valid_channels(ring->channels) || !valid_sample_rate(ring->sample_rate)) {
        return 0;
    }
    if (!check_reserved) return 1;
    for (uint32_t index = 0; index < HIBIKI_DRIVER_STREAM_RING_RESERVED_COUNT_V1;
         ++index) {
        if (ring->reserved[index] != 0U) return 0;
    }
    return 1;
}

size_t hibiki_driver_stream_ring_region_size_v1(void) {
    return sizeof(struct hibiki_driver_stream_ring_v1);
}

int hibiki_driver_stream_ring_init_v1(
    struct hibiki_driver_stream_ring_v1* const ring,
    const size_t region_bytes,
    const uint32_t channels,
    const uint32_t sample_rate) {
    if (ring == NULL || region_bytes < sizeof(*ring) || !valid_channels(channels) ||
        !valid_sample_rate(sample_rate)) {
        return HIBIKI_DRIVER_STREAM_RING_REJECTED_V1;
    }
    memset(ring, 0, sizeof(*ring));
    ring->magic = HIBIKI_DRIVER_STREAM_RING_MAGIC_V1;
    ring->abi_version = HIBIKI_DRIVER_STREAM_RING_ABI_V1;
    ring->channels = channels;
    ring->sample_rate = sample_rate;
    ring->slot_count = HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1;
    ring->slot_capacity_bytes =
        HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1;
    return HIBIKI_DRIVER_STREAM_RING_OK_V1;
}

int hibiki_driver_stream_ring_push_v1(
    struct hibiki_driver_stream_ring_v1* const ring,
    const size_t region_bytes,
    const uint8_t* const packet,
    const size_t packet_bytes) {
    if (!valid_ring(ring, region_bytes, 1)) {
        return HIBIKI_DRIVER_STREAM_RING_INVALID_V1;
    }
    if (packet == NULL || packet_bytes <= HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 ||
        packet_bytes > HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1 ||
        !hibiki_driver_stream_packet_validate_v1(packet, packet_bytes)) {
        return HIBIKI_DRIVER_STREAM_RING_REJECTED_V1;
    }

    const uint32_t producer = load_acquire(&ring->producer_sequence);
    const uint32_t consumer = load_acquire(&ring->consumer_sequence);
    if ((uint32_t)(producer - consumer) >= HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1) {
        increment_relaxed(&ring->overrun_count);
        return HIBIKI_DRIVER_STREAM_RING_OVERRUN_V1;
    }

    struct hibiki_driver_stream_ring_slot_v1* const slot =
        &ring->slots[producer % HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1];
    slot->packet_bytes = (uint32_t)packet_bytes;
    memcpy(slot->packet, packet, packet_bytes);
    store_release(&slot->ready_sequence, producer + 1U);
    store_release(&ring->producer_sequence, producer + 1U);
    return HIBIKI_DRIVER_STREAM_RING_OK_V1;
}

int hibiki_driver_stream_ring_pop_v1(
    struct hibiki_driver_stream_ring_v1* const ring,
    const size_t region_bytes,
    uint8_t* const packet_storage,
    const size_t storage_capacity,
    size_t* const packet_bytes,
    uint32_t* const silence) {
    if (packet_bytes != NULL) *packet_bytes = 0U;
    if (silence != NULL) *silence = 0U;
    if (!valid_ring(ring, region_bytes, 1)) {
        return HIBIKI_DRIVER_STREAM_RING_INVALID_V1;
    }
    if (packet_storage == NULL) {
        return HIBIKI_DRIVER_STREAM_RING_INVALID_V1;
    }
    if (storage_capacity <
        HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1) {
        return HIBIKI_DRIVER_STREAM_RING_REJECTED_V1;
    }

    const uint32_t consumer = load_acquire(&ring->consumer_sequence);
    const uint32_t producer = load_acquire(&ring->producer_sequence);
    if (consumer == producer) {
        if (silence != NULL) *silence = 1U;
        increment_relaxed(&ring->underrun_count);
        return HIBIKI_DRIVER_STREAM_RING_UNDERRUN_V1;
    }

    struct hibiki_driver_stream_ring_slot_v1* const slot =
        &ring->slots[consumer % HIBIKI_DRIVER_STREAM_RING_SLOT_COUNT_V1];
    if (load_acquire(&slot->ready_sequence) != consumer + 1U ||
        slot->reserved[0] != 0U || slot->reserved[1] != 0U ||
        slot->packet_bytes <= HIBIKI_DRIVER_STREAM_HEADER_BYTES_V1 ||
        slot->packet_bytes > HIBIKI_DRIVER_STREAM_RING_SLOT_CAPACITY_BYTES_V1) {
        return HIBIKI_DRIVER_STREAM_RING_REJECTED_V1;
    }
    if (storage_capacity < slot->packet_bytes) {
        return HIBIKI_DRIVER_STREAM_RING_REJECTED_V1;
    }

    memcpy(packet_storage, slot->packet, slot->packet_bytes);
    *packet_bytes = slot->packet_bytes;
    memset(slot->packet, 0, slot->packet_bytes);
    slot->packet_bytes = 0U;
    store_release(&ring->consumer_sequence, consumer + 1U);
    return HIBIKI_DRIVER_STREAM_RING_OK_V1;
}

void hibiki_driver_stream_ring_reset_v1(
    struct hibiki_driver_stream_ring_v1* const ring) {
    if (ring == NULL) return;
    ring->producer_sequence = 0U;
    ring->consumer_sequence = 0U;
    ring->overrun_count = 0U;
    ring->underrun_count = 0U;
    memset(ring->slots, 0, sizeof(ring->slots));
}

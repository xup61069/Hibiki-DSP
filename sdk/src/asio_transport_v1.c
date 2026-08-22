// SPDX-License-Identifier: Apache-2.0

#include "hibiki/asio_transport_v1.h"

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
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}
static void store_release(volatile uint32_t* value, const uint32_t next) {
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}
static void increment_relaxed(volatile uint32_t* value) {
    __atomic_add_fetch(value, 1U, __ATOMIC_RELAXED);
}
#endif

static int valid_format(const uint32_t channels,
                        const uint32_t sample_rate,
                        const uint32_t frames) {
    const int valid_channels = channels == 1U || channels == 2U || channels == 6U || channels == 8U;
    const int valid_rate = sample_rate == 44100U || sample_rate == 48000U ||
                           sample_rate == 96000U || sample_rate == 192000U;
    return valid_channels && valid_rate && frames > 0U && frames <= HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1;
}

size_t hibiki_asio_transport_region_size_v1(void) {
    return sizeof(struct hibiki_asio_transport_region_v1);
}

int hibiki_asio_transport_init_v1(
    struct hibiki_asio_transport_region_v1* const region,
    const size_t region_bytes,
    const uint32_t channels,
    const uint32_t sample_rate,
    const uint32_t frames_per_buffer) {
    if (region == NULL || region_bytes < sizeof(*region) ||
        !valid_format(channels, sample_rate, frames_per_buffer)) {
        return 0;
    }
    memset(region, 0, sizeof(*region));
    region->magic = HIBIKI_ASIO_TRANSPORT_MAGIC_V1;
    region->abi_version = HIBIKI_ASIO_TRANSPORT_ABI_V1;
    region->size_bytes = (uint32_t)sizeof(*region);
    region->channels = channels;
    region->sample_rate = sample_rate;
    region->frames_per_buffer = frames_per_buffer;
    return 1;
}

static int valid_region(const struct hibiki_asio_transport_region_v1* const region,
                        const size_t region_bytes) {
    return region != NULL && region_bytes >= sizeof(*region) &&
           region->magic == HIBIKI_ASIO_TRANSPORT_MAGIC_V1 &&
           region->abi_version == HIBIKI_ASIO_TRANSPORT_ABI_V1 &&
           region->size_bytes == sizeof(*region) &&
           region->reserved == 0U &&
           valid_format(region->channels, region->sample_rate, region->frames_per_buffer);
}

int hibiki_asio_transport_push_planar_v1(
    struct hibiki_asio_transport_region_v1* const region,
    const size_t region_bytes,
    const float* const* const channel_buffers,
    const uint32_t channels,
    const uint32_t frames) {
    if (!valid_region(region, region_bytes) || channel_buffers == NULL ||
        channels != region->channels || frames != region->frames_per_buffer) {
        return 0;
    }
    for (uint32_t channel = 0U; channel < channels; ++channel) {
        if (channel_buffers[channel] == NULL) return 0;
    }
    const uint32_t producer = load_acquire(&region->producer_sequence);
    const uint32_t consumer = load_acquire(&region->consumer_sequence);
    if ((uint32_t)(producer - consumer) >= HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1) {
        increment_relaxed(&region->dropped_blocks);
        return 0;
    }
    struct hibiki_asio_transport_slot_v1* const slot =
        &region->slots[producer % HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1];
    for (uint32_t frame = 0U; frame < frames; ++frame) {
        for (uint32_t channel = 0U; channel < channels; ++channel) {
            slot->samples[frame * channels + channel] = channel_buffers[channel][frame];
        }
    }
    slot->frames = frames;
    slot->channels = channels;
    slot->sample_rate = region->sample_rate;
    store_release(&slot->ready_sequence, producer + 1U);
    store_release(&region->producer_sequence, producer + 1U);
    return 1;
}

int hibiki_asio_transport_pop_interleaved_v1(
    struct hibiki_asio_transport_region_v1* const region,
    const size_t region_bytes,
    float* const interleaved,
    const uint32_t output_capacity_frames,
    uint32_t* const output_frames,
    uint32_t* const output_channels,
    uint32_t* const output_sample_rate) {
    if (!valid_region(region, region_bytes) || interleaved == NULL || output_frames == NULL ||
        output_channels == NULL || output_sample_rate == NULL) {
        return 0;
    }
    *output_frames = 0U;
    *output_channels = 0U;
    *output_sample_rate = 0U;
    const uint32_t consumer = load_acquire(&region->consumer_sequence);
    const uint32_t producer = load_acquire(&region->producer_sequence);
    if (consumer == producer) return 0;
    struct hibiki_asio_transport_slot_v1* const slot =
        &region->slots[consumer % HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1];
    if (load_acquire(&slot->ready_sequence) != consumer + 1U ||
        slot->frames > output_capacity_frames || slot->channels == 0U || slot->channels > 8U) {
        return 0;
    }
    const uint32_t samples = slot->frames * slot->channels;
    memcpy(interleaved, slot->samples, (size_t)samples * sizeof(float));
    *output_frames = slot->frames;
    *output_channels = slot->channels;
    *output_sample_rate = slot->sample_rate;
    store_release(&region->consumer_sequence, consumer + 1U);
    return 1;
}

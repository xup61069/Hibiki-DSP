#ifndef HIBIKI_ASIO_TRANSPORT_V1_H
#define HIBIKI_ASIO_TRANSPORT_V1_H

// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>
#include <stdint.h>

#define HIBIKI_ASIO_TRANSPORT_ABI_V1 1u
#define HIBIKI_ASIO_TRANSPORT_MAGIC_V1 0x314f4948u /* HIO1 */
#define HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1 4u
#define HIBIKI_ASIO_TRANSPORT_MAX_CHANNELS_V1 8u
#define HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1 4096u

struct hibiki_asio_transport_slot_v1 {
    volatile uint32_t ready_sequence;
    uint32_t frames;
    uint32_t channels;
    uint32_t sample_rate;
    float samples[HIBIKI_ASIO_TRANSPORT_MAX_CHANNELS_V1 * HIBIKI_ASIO_TRANSPORT_MAX_FRAMES_V1];
};

struct hibiki_asio_transport_region_v1 {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size_bytes;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t frames_per_buffer;
    volatile uint32_t producer_sequence;
    volatile uint32_t consumer_sequence;
    volatile uint32_t dropped_blocks;
    uint32_t reserved;
    struct hibiki_asio_transport_slot_v1 slots[HIBIKI_ASIO_TRANSPORT_SLOT_COUNT_V1];
};

#ifdef __cplusplus
extern "C" {
#endif

size_t hibiki_asio_transport_region_size_v1(void);

int hibiki_asio_transport_init_v1(
    struct hibiki_asio_transport_region_v1* region,
    size_t region_bytes,
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t frames_per_buffer);

int hibiki_asio_transport_push_planar_v1(
    struct hibiki_asio_transport_region_v1* region,
    size_t region_bytes,
    const float* const* channel_buffers,
    uint32_t channels,
    uint32_t frames);

int hibiki_asio_transport_pop_interleaved_v1(
    struct hibiki_asio_transport_region_v1* region,
    size_t region_bytes,
    float* interleaved,
    uint32_t output_capacity_frames,
    uint32_t* output_frames,
    uint32_t* output_channels,
    uint32_t* output_sample_rate);

#ifdef __cplusplus
}
#endif

#endif

#ifndef HIBIKI_ENDPOINT_TOPOLOGY_V1_H
#define HIBIKI_ENDPOINT_TOPOLOGY_V1_H

// SPDX-License-Identifier: MS-PL

#include <stdint.h>

#include "hibiki/driver_control_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HIBIKI_ENDPOINT_TOPOLOGY_ABI_V1 1u
#define HIBIKI_ENDPOINT_TOPOLOGY_COUNT_V1 4u

enum hibiki_endpoint_kind_v1 {
    HIBIKI_ENDPOINT_MAIN_RENDER_V1 = 1,
    HIBIKI_ENDPOINT_LOW_LATENCY_RENDER_V1 = 2,
    HIBIKI_ENDPOINT_SURROUND_RENDER_V1 = 3,
    HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1 = 4
};

enum hibiki_endpoint_direction_v1 {
    HIBIKI_ENDPOINT_DIRECTION_RENDER_V1 = 0,
    HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1 = 1
};

enum hibiki_endpoint_sample_rate_flag_v1 {
    HIBIKI_ENDPOINT_RATE_44100_V1 = 1u << 0,
    HIBIKI_ENDPOINT_RATE_48000_V1 = 1u << 1,
    HIBIKI_ENDPOINT_RATE_96000_V1 = 1u << 2,
    HIBIKI_ENDPOINT_RATE_192000_V1 = 1u << 3
};

// Windows KSAUDIO_SPEAKER_* channel masks. These values are kept here so the
// SYSVAD-derived topology and user-space never infer ordering from a count.
#define HIBIKI_CHANNEL_MASK_MONO_V1 UINT64_C(0x0000000000000004)
#define HIBIKI_CHANNEL_MASK_STEREO_V1 UINT64_C(0x0000000000000003)
#define HIBIKI_CHANNEL_MASK_51_V1 UINT64_C(0x000000000000003f)
#define HIBIKI_CHANNEL_MASK_71_V1 UINT64_C(0x000000000000063f)

struct hibiki_endpoint_topology_v1 {
    uint32_t abi_version;
    uint32_t endpoint_kind;
    uint32_t direction;
    uint32_t channel_count;
    uint32_t sample_rate;
    uint32_t supported_sample_rates;
    uint32_t frames_per_buffer;
    uint64_t channel_mask;
    char endpoint_guid[HIBIKI_ENDPOINT_GUID_CAPACITY];
};

uint32_t hibiki_endpoint_topology_count_v1(void);

int hibiki_endpoint_topology_get_v1(
    uint32_t index,
    struct hibiki_endpoint_topology_v1* topology);

int hibiki_endpoint_topology_validate_v1(
    const struct hibiki_endpoint_topology_v1* topology);

#ifdef __cplusplus
}
#endif

#endif

// SPDX-License-Identifier: MS-PL

#include "hibiki/endpoint_topology_v1.h"

#include <string.h>

static const struct hibiki_endpoint_topology_v1 k_topologies[] = {
    {HIBIKI_ENDPOINT_TOPOLOGY_ABI_V1,
     HIBIKI_ENDPOINT_MAIN_RENDER_V1,
     HIBIKI_ENDPOINT_DIRECTION_RENDER_V1,
     2U,
     48000U,
     HIBIKI_ENDPOINT_RATE_44100_V1 | HIBIKI_ENDPOINT_RATE_48000_V1 |
         HIBIKI_ENDPOINT_RATE_96000_V1 | HIBIKI_ENDPOINT_RATE_192000_V1,
     256U,
     HIBIKI_CHANNEL_MASK_STEREO_V1,
     "8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10"},
    {HIBIKI_ENDPOINT_TOPOLOGY_ABI_V1,
     HIBIKI_ENDPOINT_LOW_LATENCY_RENDER_V1,
     HIBIKI_ENDPOINT_DIRECTION_RENDER_V1,
     2U,
     48000U,
     HIBIKI_ENDPOINT_RATE_44100_V1 | HIBIKI_ENDPOINT_RATE_48000_V1 |
         HIBIKI_ENDPOINT_RATE_96000_V1,
     64U,
     HIBIKI_CHANNEL_MASK_STEREO_V1,
     "6d5706a4-b661-4bf6-9c2d-9c31b8f7df21"},
    {HIBIKI_ENDPOINT_TOPOLOGY_ABI_V1,
     HIBIKI_ENDPOINT_SURROUND_RENDER_V1,
     HIBIKI_ENDPOINT_DIRECTION_RENDER_V1,
     8U,
     48000U,
     HIBIKI_ENDPOINT_RATE_44100_V1 | HIBIKI_ENDPOINT_RATE_48000_V1 |
         HIBIKI_ENDPOINT_RATE_96000_V1 | HIBIKI_ENDPOINT_RATE_192000_V1,
     256U,
     HIBIKI_CHANNEL_MASK_71_V1,
     "d4a21e0f-83e5-4a6e-92dc-44d13f9e6c93"},
    {HIBIKI_ENDPOINT_TOPOLOGY_ABI_V1,
     HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1,
     HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1,
     2U,
     48000U,
     HIBIKI_ENDPOINT_RATE_44100_V1 | HIBIKI_ENDPOINT_RATE_48000_V1 |
         HIBIKI_ENDPOINT_RATE_96000_V1 | HIBIKI_ENDPOINT_RATE_192000_V1,
     128U,
     HIBIKI_CHANNEL_MASK_STEREO_V1,
     "5e90de25-0e88-4892-8b0e-a1d521cb3f40"}
};

static int valid_kind(const uint32_t kind) {
    return kind >= HIBIKI_ENDPOINT_MAIN_RENDER_V1 &&
           kind <= HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1;
}

static int valid_direction(const uint32_t direction) {
    return direction == HIBIKI_ENDPOINT_DIRECTION_RENDER_V1 ||
           direction == HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1;
}

static int valid_channels_and_mask(const uint32_t channels, const uint64_t mask) {
    return (channels == 2U && (mask == HIBIKI_CHANNEL_MASK_STEREO_V1 ||
                               mask == HIBIKI_CHANNEL_MASK_MONO_V1)) ||
           (channels == 6U && mask == HIBIKI_CHANNEL_MASK_51_V1) ||
           (channels == 8U && mask == HIBIKI_CHANNEL_MASK_71_V1);
}

static uint32_t rate_flag(const uint32_t rate) {
    switch (rate) {
        case 44100U: return HIBIKI_ENDPOINT_RATE_44100_V1;
        case 48000U: return HIBIKI_ENDPOINT_RATE_48000_V1;
        case 96000U: return HIBIKI_ENDPOINT_RATE_96000_V1;
        case 192000U: return HIBIKI_ENDPOINT_RATE_192000_V1;
        default: return 0U;
    }
}

uint32_t hibiki_endpoint_topology_count_v1(void) {
    return HIBIKI_ENDPOINT_TOPOLOGY_COUNT_V1;
}

int hibiki_endpoint_topology_validate_v1(
    const struct hibiki_endpoint_topology_v1* const topology) {
    if (topology == NULL || topology->abi_version != HIBIKI_ENDPOINT_TOPOLOGY_ABI_V1 ||
        !valid_kind(topology->endpoint_kind) || !valid_direction(topology->direction) ||
        !valid_channels_and_mask(topology->channel_count, topology->channel_mask) ||
        rate_flag(topology->sample_rate) == 0U ||
        (topology->supported_sample_rates & rate_flag(topology->sample_rate)) == 0U ||
        topology->supported_sample_rates == 0U || topology->frames_per_buffer == 0U ||
        topology->frames_per_buffer > 4096U ||
        memchr(topology->endpoint_guid, '\0', HIBIKI_ENDPOINT_GUID_CAPACITY) == NULL) {
        return 0;
    }
    if (topology->direction == HIBIKI_ENDPOINT_DIRECTION_CAPTURE_V1 &&
        topology->endpoint_kind != HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1) {
        return 0;
    }
    if (topology->direction == HIBIKI_ENDPOINT_DIRECTION_RENDER_V1 &&
        topology->endpoint_kind == HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1) {
        return 0;
    }
    return 1;
}

int hibiki_endpoint_topology_get_v1(
    const uint32_t index,
    struct hibiki_endpoint_topology_v1* const topology) {
    if (topology == NULL || index >= HIBIKI_ENDPOINT_TOPOLOGY_COUNT_V1) return 0;
    *topology = k_topologies[index];
    return hibiki_endpoint_topology_validate_v1(topology);
}

#ifndef HIBIKI_WAVERT_ENDPOINT_STATE_V1_H
#define HIBIKI_WAVERT_ENDPOINT_STATE_V1_H

// SPDX-License-Identifier: MS-PL

#include <stdint.h>

#include "hibiki/driver_control_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pure control-state core for the future WDK WaveRT adapter. It has no COM,
// heap allocation or user-space linkage and is safe to embed behind a
// versioned KS property handler.
struct hibiki_wavert_endpoint_state_v1 {
    char endpoint_guid[HIBIKI_ENDPOINT_GUID_CAPACITY];
    char last_event_context_guid[HIBIKI_ENDPOINT_GUID_CAPACITY];
    uint32_t channel_count;
    uint32_t sample_rate;
    int32_t requested_db_q16_16;
    int32_t safety_ceiling_db_q16_16;
    int32_t effective_db_q16_16;
    uint32_t mute;
    uint64_t generation;
    uint32_t actuator;
};

int hibiki_wavert_endpoint_state_init_v1(
    struct hibiki_wavert_endpoint_state_v1* state,
    const char* endpoint_guid,
    uint32_t channel_count,
    uint32_t sample_rate,
    uint32_t actuator);

int hibiki_wavert_endpoint_state_apply_volume_v1(
    struct hibiki_wavert_endpoint_state_v1* state,
    int32_t requested_db_q16_16,
    int32_t safety_ceiling_db_q16_16,
    uint32_t mute,
    uint64_t generation,
    const char* event_context_guid);

#ifdef __cplusplus
}
#endif

#endif

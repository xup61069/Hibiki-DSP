// SPDX-License-Identifier: MS-PL

#include "hibiki/wavert_endpoint_state_v1.h"

#include <string.h>

static int valid_channels(const uint32_t channels) {
    return channels == 2U || channels == 6U || channels == 8U;
}

static int valid_rate(const uint32_t rate) {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

static int valid_db(const int32_t value) {
    return value >= (-144 * 65536) && value <= (12 * 65536);
}

static int copy_guid(char* const destination, const char* const source) {
    size_t index;
    if (destination == NULL || source == NULL) {
        return 0;
    }
    for (index = 0U; index + 1U < HIBIKI_ENDPOINT_GUID_CAPACITY; ++index) {
        destination[index] = source[index];
        if (source[index] == '\0') {
            return 1;
        }
    }
    destination[HIBIKI_ENDPOINT_GUID_CAPACITY - 1U] = '\0';
    return source[index] == '\0';
}

static int32_t effective_db(const int32_t requested, const int32_t ceiling) {
    int32_t value = requested < ceiling ? requested : ceiling;
    if (value < (-144 * 65536)) {
        return -144 * 65536;
    }
    if (value > (12 * 65536)) {
        return 12 * 65536;
    }
    return value;
}

int hibiki_wavert_endpoint_state_init_v1(
    struct hibiki_wavert_endpoint_state_v1* const state,
    const char* const endpoint_guid,
    const uint32_t channel_count,
    const uint32_t sample_rate,
    const uint32_t actuator) {
    if (state == NULL || !valid_channels(channel_count) || !valid_rate(sample_rate) ||
        actuator > HIBIKI_ACTUATOR_STRICT_DIRECT || !copy_guid(state->endpoint_guid, endpoint_guid)) {
        return 0;
    }
    memset(state->last_event_context_guid, 0, sizeof(state->last_event_context_guid));
    state->channel_count = channel_count;
    state->sample_rate = sample_rate;
    state->requested_db_q16_16 = -60 * 65536;
    state->safety_ceiling_db_q16_16 = 0;
    state->effective_db_q16_16 = -60 * 65536;
    state->mute = 0U;
    state->generation = 1U;
    state->actuator = actuator;
    return 1;
}

int hibiki_wavert_endpoint_state_apply_volume_v1(
    struct hibiki_wavert_endpoint_state_v1* const state,
    const int32_t requested_db_q16_16,
    const int32_t safety_ceiling_db_q16_16,
    const uint32_t mute,
    const uint64_t generation,
    const char* const event_context_guid) {
    if (state == NULL || !valid_db(requested_db_q16_16) || !valid_db(safety_ceiling_db_q16_16) ||
        mute > 1U || generation < state->generation) {
        return 0;
    }
    state->requested_db_q16_16 = requested_db_q16_16;
    state->safety_ceiling_db_q16_16 = safety_ceiling_db_q16_16;
    state->effective_db_q16_16 = state->actuator == HIBIKI_ACTUATOR_STRICT_DIRECT
                                     ? 0
                                     : effective_db(requested_db_q16_16, safety_ceiling_db_q16_16);
    state->mute = mute;
    state->generation = generation;
    if (event_context_guid == NULL) {
        memset(state->last_event_context_guid, 0, sizeof(state->last_event_context_guid));
    } else if (!copy_guid(state->last_event_context_guid, event_context_guid)) {
        return 0;
    }
    return 1;
}

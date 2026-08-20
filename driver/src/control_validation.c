// SPDX-License-Identifier: MS-PL

#include "hibiki/driver_validation_v1.h"

#include <string.h>

static int valid_channels(const uint32_t channels) {
    return channels == 2U || channels == 6U || channels == 8U;
}

static int valid_rate(const uint32_t rate) {
    return rate == 44100U || rate == 48000U || rate == 96000U || rate == 192000U;
}

int hibiki_driver_validate_endpoint_state_v1(
    const struct hibiki_driver_endpoint_state_v1* state,
    const size_t available_bytes) {
    if (state == NULL || available_bytes < sizeof(*state) ||
        state->header.size_bytes != sizeof(*state) ||
        state->header.abi_version != HIBIKI_DRIVER_CONTROL_ABI_V1 ||
        (state->header.message_type != HIBIKI_DRIVER_ENDPOINT_STATE &&
         state->header.message_type != HIBIKI_DRIVER_VOLUME_NOTIFICATION) ||
        memchr(state->endpoint_guid, '\0', HIBIKI_ENDPOINT_GUID_CAPACITY) == NULL ||
        memchr(state->event_context_guid, '\0', HIBIKI_ENDPOINT_GUID_CAPACITY) == NULL ||
        !valid_channels(state->channel_count) || !valid_rate(state->sample_rate) ||
        state->frames_per_buffer == 0U || state->frames_per_buffer > 4096U ||
        state->actuator > HIBIKI_ACTUATOR_STRICT_DIRECT) {
        return 0;
    }
    return 1;
}

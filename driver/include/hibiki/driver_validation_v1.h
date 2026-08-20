#ifndef HIBIKI_DRIVER_VALIDATION_V1_H
#define HIBIKI_DRIVER_VALIDATION_V1_H

// SPDX-License-Identifier: MS-PL

#include <stddef.h>

#include "hibiki/driver_control_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

int hibiki_driver_validate_endpoint_state_v1(
    const struct hibiki_driver_endpoint_state_v1* state,
    size_t available_bytes);

#ifdef __cplusplus
}
#endif

#endif

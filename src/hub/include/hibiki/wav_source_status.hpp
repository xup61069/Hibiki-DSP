#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_status.hpp"

namespace hibiki {

// Map the bounded WAV-source lifecycle to the user-space route state. A
// requested source that was not prepared is terminally unavailable; a
// prepared source remains pending until a block has rendered successfully.
[[nodiscard]] constexpr ControlRouteHealthStateV1 wav_source_route_state_v1(
    const bool requested,
    const bool prepared,
    const bool rendered) noexcept {
    if (!requested || !prepared) return ControlRouteHealthStateV1::Unavailable;
    return rendered ? ControlRouteHealthStateV1::Ready
                    : ControlRouteHealthStateV1::Pending;
}

}  // namespace hibiki

#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>

#include "hibiki/control_status.hpp"

namespace hibiki {

// A VST3 route is Ready only while the current worker session is usable and
// its lane handshake is Ready. Historical processed blocks must not preserve
// Ready after setup or exchange quarantine.
[[nodiscard]] constexpr ControlRouteHealthStateV1 vst3_route_state_v1(
    const bool launched,
    const bool host_processable,
    const bool worker_ready,
    const std::uint64_t processed_blocks,
    const std::uint64_t failed_blocks) noexcept {
    if (!launched) {
        return failed_blocks > 0U ? ControlRouteHealthStateV1::Degraded
                                  : ControlRouteHealthStateV1::Pending;
    }
    if (!host_processable || !worker_ready) {
        return ControlRouteHealthStateV1::Degraded;
    }
    return processed_blocks > 0U ? ControlRouteHealthStateV1::Ready
                                 : ControlRouteHealthStateV1::Pending;
}

}  // namespace hibiki

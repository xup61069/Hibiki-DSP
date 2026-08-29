#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/windows_wasapi_handoff.hpp"

namespace hibiki {

// Source routes may use cumulative rendered counters as diagnostics, but a
// current Ready state requires the active sink to be healthy now.
[[nodiscard]] constexpr bool wasapi_source_sink_ready_v1(
    const WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    if (snapshot.state != WasapiSinkHandoffStateV1::Synced || snapshot.active_slot > 1U) {
        return false;
    }
    const auto& active = snapshot.active_slot == 0U ? snapshot.primary : snapshot.secondary;
    return active.running && active.endpoint_ready && !active.degraded;
}

// A degraded handoff or active worker must be surfaced as Degraded. Invalid
// active-slot metadata also fails closed instead of selecting an arbitrary
// worker as the source-route health authority.
[[nodiscard]] constexpr bool wasapi_source_sink_degraded_v1(
    const WasapiSinkHandoffSnapshotV1& snapshot) noexcept {
    if (snapshot.state == WasapiSinkHandoffStateV1::Degraded || snapshot.active_slot > 1U) {
        return true;
    }
    const auto& active = snapshot.active_slot == 0U ? snapshot.primary : snapshot.secondary;
    return active.degraded;
}

}  // namespace hibiki

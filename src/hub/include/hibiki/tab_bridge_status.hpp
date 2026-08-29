#pragma once

// SPDX-License-Identifier: GPL-3.0-only

namespace hibiki {

// A requested tab suppressor is a startup prerequisite. Do not bind the
// listener when its configuration failed, because accepting packets would
// silently bypass the explicitly requested effect.
[[nodiscard]] constexpr bool tab_bridge_start_allowed_v1(
    const bool suppressor_requested,
    const bool suppressor_configured) noexcept {
    return !suppressor_requested || suppressor_configured;
}

}  // namespace hibiki

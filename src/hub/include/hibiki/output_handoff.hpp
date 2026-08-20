#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_switch.hpp"
#include "hibiki/output_crossfade.hpp"

#include <cstddef>

namespace hibiki {

enum class OutputHandoffStateV1 : std::uint8_t {
    Idle,
    Preparing,
    Fading,
    Committed,
    RolledBack,
    Degraded,
};

// Combines endpoint transaction and audio-side crossfade. A new endpoint is
// never committed until its caller-owned buffers have completed the fade.
class OutputHandoffCoordinatorV1 final {
public:
    [[nodiscard]] bool begin(DeviceTargetV1 target,
                             std::uint32_t crossfade_ms = 30U) noexcept;
    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool process(const float* old_interleaved,
                               const float* new_interleaved,
                               float* output_interleaved,
                               std::size_t frames) noexcept;
    [[nodiscard]] bool commit() noexcept;
    void rollback() noexcept;

    [[nodiscard]] OutputHandoffStateV1 state() const noexcept { return state_; }
    [[nodiscard]] const DeviceTargetV1& active_target() const noexcept {
        return transaction_.active_target();
    }
    [[nodiscard]] const OutputCrossfadeSnapshotV1& crossfade() const noexcept {
        return crossfade_.snapshot();
    }

private:
    DeviceSwitchTransaction transaction_{};
    OutputCrossfade crossfade_{};
    OutputHandoffStateV1 state_{OutputHandoffStateV1::Idle};
};

}  // namespace hibiki

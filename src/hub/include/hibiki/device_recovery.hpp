#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/device_switch.hpp"
#include "hibiki/volume_state.hpp"

#include <cstdint>

namespace hibiki {

enum class DeviceRecoveryEventKind : std::uint8_t {
    DefaultChanged,
    EndpointAdded,
    EndpointRemoved,
    EndpointStateChanged,
    EndpointInvalidated,
    FormatChanged,
    AudioServiceRestarted,
};

struct DeviceRecoveryEventV1 {
    std::uint64_t sequence{0};
    DeviceRecoveryEventKind kind{DeviceRecoveryEventKind::EndpointInvalidated};
    bool affects_active_endpoint{false};
};

enum class DeviceRecoveryState : std::uint8_t {
    Stable,
    RebindPending,
    Rebinding,
    Degraded,
};

// Worker-side recovery coordinator. Windows callbacks only produce an event
// snapshot; this object owns the transactional rebind and safe restart policy.
// It is deliberately platform-neutral so the same state machine can be tested
// without a physical endpoint or Audio Service.
class DeviceRecoveryCoordinator final {
public:
    [[nodiscard]] DeviceRecoveryState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t last_event_sequence() const noexcept {
        return last_event_sequence_;
    }
    [[nodiscard]] const DeviceSwitchTransaction& transaction() const noexcept {
        return transaction_;
    }

    [[nodiscard]] bool observe(const DeviceRecoveryEventV1& event) noexcept;
    [[nodiscard]] bool begin_rebind(DeviceTargetV1 target) noexcept;
    [[nodiscard]] bool prepare() noexcept;
    [[nodiscard]] bool commit() noexcept;
    void rollback() noexcept;
    void mark_degraded() noexcept { state_ = DeviceRecoveryState::Degraded; }

    // On invalidation/restart, resume no louder than the configured safe-start
    // level. The result is muted until the control plane explicitly unmutes it.
    [[nodiscard]] OutputGroupVolumeStateV1 safe_restart_state(
        OutputGroupVolumeStateV1 state,
        double safe_start_db = -60.0) const noexcept;

private:
    DeviceRecoveryState state_{DeviceRecoveryState::Stable};
    DeviceSwitchTransaction transaction_{};
    std::uint64_t last_event_sequence_{0};
};

}  // namespace hibiki

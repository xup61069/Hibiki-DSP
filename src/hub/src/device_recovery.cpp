#include "hibiki/device_recovery.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hibiki {

bool DeviceRecoveryCoordinator::observe(const DeviceRecoveryEventV1& event) noexcept {
    if (event.sequence == 0 || event.sequence <= last_event_sequence_) {
        return false;
    }
    last_event_sequence_ = event.sequence;

    switch (event.kind) {
        case DeviceRecoveryEventKind::DefaultChanged:
        case DeviceRecoveryEventKind::EndpointAdded:
            state_ = DeviceRecoveryState::RebindPending;
            return true;
        case DeviceRecoveryEventKind::EndpointRemoved:
        case DeviceRecoveryEventKind::EndpointInvalidated:
        case DeviceRecoveryEventKind::FormatChanged:
        case DeviceRecoveryEventKind::AudioServiceRestarted:
            if (event.affects_active_endpoint || transaction_.active_target().endpoint_id.empty()) {
                state_ = DeviceRecoveryState::RebindPending;
                return true;
            }
            return false;
        case DeviceRecoveryEventKind::EndpointStateChanged:
            if (event.affects_active_endpoint) {
                state_ = DeviceRecoveryState::RebindPending;
                return true;
            }
            return false;
    }
    return false;
}

bool DeviceRecoveryCoordinator::begin_rebind(DeviceTargetV1 target) noexcept {
    if (state_ != DeviceRecoveryState::RebindPending &&
        state_ != DeviceRecoveryState::Degraded) {
        return false;
    }
    if (!transaction_.begin(std::move(target))) {
        state_ = DeviceRecoveryState::Degraded;
        return false;
    }
    state_ = DeviceRecoveryState::Rebinding;
    return true;
}

bool DeviceRecoveryCoordinator::begin_rebind(const PhysicalDeviceCatalogV1& catalog,
                                             const std::string_view endpoint_id) noexcept {
    try {
        if (endpoint_id.empty()) return false;
        const std::string target_id(endpoint_id);
        if (!catalog.selectable(target_id, PhysicalDeviceFlowV1::Render)) return false;
        const auto* const descriptor = catalog.find(target_id);
        if (descriptor == nullptr) return false;
        return begin_rebind(DeviceTargetV1{descriptor->endpoint_id, descriptor->channels,
                                           descriptor->sample_rate, descriptor->buffer_frames});
    } catch (...) {
        return false;
    }
}

bool DeviceRecoveryCoordinator::prepare() noexcept {
    if (state_ != DeviceRecoveryState::Rebinding || !transaction_.prepare_complete()) {
        return false;
    }
    return true;
}

bool DeviceRecoveryCoordinator::commit() noexcept {
    if (state_ != DeviceRecoveryState::Rebinding || !transaction_.commit()) {
        return false;
    }
    state_ = DeviceRecoveryState::Stable;
    return true;
}

void DeviceRecoveryCoordinator::rollback() noexcept {
    transaction_.rollback();
    state_ = transaction_.active_target().endpoint_id.empty() ? DeviceRecoveryState::Degraded
                                                                : DeviceRecoveryState::Stable;
}

OutputGroupVolumeStateV1 DeviceRecoveryCoordinator::safe_restart_state(
    OutputGroupVolumeStateV1 state,
    const double safe_start_db) const noexcept {
    const double bounded_safe_start = std::isfinite(safe_start_db)
                                          ? std::clamp(safe_start_db, -144.0, 0.0)
                                          : -60.0;
    state.requested_db = std::isfinite(state.requested_db)
                             ? std::min(state.requested_db, bounded_safe_start)
                             : bounded_safe_start;
    state.mute = true;
    ++state.generation;
    return reconcile(state);
}

}  // namespace hibiki

#include "hibiki/device_switch.hpp"

#include <utility>

namespace hibiki {

bool DeviceSwitchTransaction::begin(DeviceTargetV1 target) noexcept {
    if (target.endpoint_id.empty() ||
        (target.channels != 2 && target.channels != 6 && target.channels != 8) ||
        target.sample_rate == 0 || target.buffer_frames == 0) {
        return false;
    }
    pending_ = std::move(target);
    state_ = (state_ == DeviceSwitchState::Synced) ? DeviceSwitchState::Rebinding
                                                    : DeviceSwitchState::Binding;
    return true;
}

bool DeviceSwitchTransaction::prepare_complete() noexcept {
    if (state_ != DeviceSwitchState::Binding && state_ != DeviceSwitchState::Rebinding) {
        return false;
    }
    state_ = DeviceSwitchState::WritePending;
    return true;
}

bool DeviceSwitchTransaction::commit() noexcept {
    if (state_ != DeviceSwitchState::WritePending) {
        return false;
    }
    active_ = pending_;
    pending_ = {};
    state_ = DeviceSwitchState::Synced;
    return true;
}

void DeviceSwitchTransaction::rollback() noexcept {
    pending_ = {};
    state_ = active_.endpoint_id.empty() ? DeviceSwitchState::Unbound : DeviceSwitchState::Synced;
}

}  // namespace hibiki

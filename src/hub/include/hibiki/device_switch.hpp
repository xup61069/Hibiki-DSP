#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>
#include <string>

namespace hibiki {

enum class DeviceSwitchState : std::uint8_t {
    Unbound,
    Binding,
    Synced,
    WritePending,
    Rebinding,
    Degraded,
};

struct DeviceTargetV1 {
    std::string endpoint_id;
    std::uint32_t channels{2};
    std::uint32_t sample_rate{48000};
    std::uint32_t buffer_frames{128};
};

class DeviceSwitchTransaction final {
public:
    [[nodiscard]] DeviceSwitchState state() const noexcept { return state_; }
    [[nodiscard]] const DeviceTargetV1& active_target() const noexcept { return active_; }

    [[nodiscard]] bool begin(DeviceTargetV1 target) noexcept;
    [[nodiscard]] bool prepare_complete() noexcept;
    [[nodiscard]] bool commit() noexcept;
    void rollback() noexcept;
    void mark_degraded() noexcept { state_ = DeviceSwitchState::Degraded; }

private:
    DeviceSwitchState state_{DeviceSwitchState::Unbound};
    DeviceTargetV1 active_{};
    DeviceTargetV1 pending_{};
};

}  // namespace hibiki

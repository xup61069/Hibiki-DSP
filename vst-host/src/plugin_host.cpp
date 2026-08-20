#include "hibiki/plugin_host.hpp"

namespace hibiki {

bool PluginHostModel::start(const PluginDescriptorV1& descriptor) {
    if (descriptor.plugin_id.empty() || descriptor.input_channels == 0 ||
        descriptor.input_channels > 8 || descriptor.output_channels == 0 ||
        descriptor.output_channels > 8 || !descriptor.trusted ||
        !descriptor.certified || descriptor.watchdog_timeout_ms == 0 ||
        descriptor.watchdog_timeout_ms > 5000 ||
        descriptor.input_channels != descriptor.output_channels || descriptor.lane_token == 0U) {
        state_ = PluginHostState::Quarantined;
        return false;
    }
    descriptor_ = descriptor;
    last_heartbeat_ms_ = 0;
    state_ = PluginHostState::Running;
    return true;
}

void PluginHostModel::stop() noexcept {
    state_ = PluginHostState::Disabled;
}

void PluginHostModel::report_crash() noexcept {
    state_ = PluginHostState::Quarantined;
}

bool PluginHostModel::heartbeat(const std::uint64_t now_ms) noexcept {
    if (!can_process() || (last_heartbeat_ms_ != 0 && now_ms < last_heartbeat_ms_)) {
        return false;
    }
    last_heartbeat_ms_ = now_ms;
    return true;
}

bool PluginHostModel::poll_watchdog(const std::uint64_t now_ms) noexcept {
    if (!can_process() || last_heartbeat_ms_ == 0 || now_ms < last_heartbeat_ms_ ||
        now_ms - last_heartbeat_ms_ <= descriptor_.watchdog_timeout_ms) {
        return false;
    }
    state_ = PluginHostState::Quarantined;
    return true;
}

bool PluginHostModel::can_process() const noexcept {
    return state_ == PluginHostState::Running;
}

bool PluginHostModel::process_passthrough(const float* const input,
                                          float* const output,
                                          const std::size_t samples) const noexcept {
    if (!can_process() || input == nullptr || output == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < samples; ++index) {
        output[index] = input[index];
    }
    return true;
}

}  // namespace hibiki

#include "hibiki/plugin_host.hpp"

namespace hibiki {

bool PluginHostModel::start(const PluginDescriptorV1& descriptor) {
    if (descriptor.plugin_id.empty() || descriptor.input_channels == 0 ||
        descriptor.input_channels > 8 || descriptor.output_channels == 0 ||
        descriptor.output_channels > 8 || !descriptor.trusted ||
        descriptor.input_channels != descriptor.output_channels) {
        state_ = PluginHostState::Quarantined;
        return false;
    }
    descriptor_ = descriptor;
    state_ = PluginHostState::Running;
    return true;
}

void PluginHostModel::stop() noexcept {
    state_ = PluginHostState::Disabled;
}

void PluginHostModel::report_crash() noexcept {
    state_ = PluginHostState::Quarantined;
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

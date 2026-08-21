#include "hibiki/plugin_host.hpp"

namespace hibiki {

bool PluginHostModel::start(const PluginDescriptorV1& descriptor) {
    worker_lane_.detach();
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
    worker_lane_.detach();
    state_ = PluginHostState::Disabled;
}

void PluginHostModel::report_crash() noexcept {
    worker_lane_.detach();
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

bool PluginHostModel::prepare_worker_session(
    Vst3SandboxProcess& sandbox,
    const double sample_rate,
    const std::uint32_t max_block_frames) noexcept {
    if (state_ != PluginHostState::Running) return false;
    return worker_lane_.prepare(
        sandbox,
        Vst3WorkerLaneConfigV1{descriptor_.lane_token, descriptor_.output_channels,
                               sample_rate, descriptor_.reported_latency_samples,
                               max_block_frames});
}

Vst3WorkerExchangeResultV1 PluginHostModel::handshake_worker(
    const std::uint64_t request_id) {
    if (state_ != PluginHostState::Running) return Vst3WorkerExchangeResultV1::not_running;
    const auto result = worker_lane_.handshake(request_id);
    if (result != Vst3WorkerExchangeResultV1::ok) report_crash();
    return result;
}

Vst3WorkerExchangeResultV1 PluginHostModel::process_worker_block(
    const std::uint64_t request_id,
    const std::uint64_t block_start,
    const std::uint32_t frames,
    const std::span<const float> input,
    const std::span<float> output) {
    if (state_ != PluginHostState::Running) return Vst3WorkerExchangeResultV1::not_running;
    const auto result = worker_lane_.process_block(request_id, block_start, frames, input, output);
    if (result != Vst3WorkerExchangeResultV1::ok) report_crash();
    return result;
}

Vst3PluginStateResultV1 PluginHostModel::capture_plugin_state(
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& identity,
    const std::uint32_t state_version,
    const std::span<const std::uint8_t> bytes) {
    if (state_ != PluginHostState::Running) return Vst3PluginStateResultV1::invalid_argument;
    return plugin_state_.capture(state_id, identity, state_version, bytes);
}

Vst3PluginStateResultV1 PluginHostModel::restore_plugin_state(
    const std::string_view state_id,
    const Vst3PluginStateIdentityV1& expected_identity,
    const std::uint32_t expected_state_version,
    const std::span<std::uint8_t> destination,
    std::size_t& bytes_written) const noexcept {
    return plugin_state_.restore(state_id, expected_identity, expected_state_version,
                                 destination, bytes_written);
}

}  // namespace hibiki

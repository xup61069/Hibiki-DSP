#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstddef>
#include <cstdint>
#include <string>

namespace hibiki {

enum class PluginHostState : std::uint8_t {
    Disabled,
    Starting,
    Running,
    Quarantined,
};

struct PluginDescriptorV1 {
    std::string plugin_id;
    std::uint32_t input_channels{2};
    std::uint32_t output_channels{2};
    std::uint32_t reported_latency_samples{0};
    bool trusted{false};
};

class PluginHostModel final {
public:
    [[nodiscard]] bool start(const PluginDescriptorV1& descriptor);
    void stop() noexcept;
    void report_crash() noexcept;
    [[nodiscard]] bool can_process() const noexcept;
    [[nodiscard]] bool process_passthrough(const float* input,
                                            float* output,
                                            std::size_t samples) const noexcept;
    [[nodiscard]] PluginHostState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t latency_samples() const noexcept {
        return descriptor_.reported_latency_samples;
    }

private:
    PluginDescriptorV1 descriptor_{};
    PluginHostState state_{PluginHostState::Disabled};
};

}  // namespace hibiki

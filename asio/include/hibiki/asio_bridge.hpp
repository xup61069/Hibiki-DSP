#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/volume_state.hpp"

#include <cstddef>
#include <cstdint>

namespace hibiki {

struct AsioStreamConfigV1 {
    std::uint32_t sample_rate{48000};
    std::uint32_t channels{2};
    std::uint32_t frames_per_buffer{128};
};

struct AsioBridgeStateV1 {
    std::uint32_t schema_version{1};
    AsioStreamConfigV1 stream{};
    double effective_db{-144.0};
    bool mute{false};
    bool prepared{false};
};

class AsioBridgeModel final {
public:
    [[nodiscard]] bool prepare(const AsioStreamConfigV1& config) noexcept;
    void release() noexcept;
    void apply_group_volume(const OutputGroupVolumeStateV1& volume) noexcept;
    [[nodiscard]] bool process_interleaved(const float* input,
                                           float* output,
                                           std::size_t frames) const noexcept;
    [[nodiscard]] const AsioBridgeStateV1& state() const noexcept { return state_; }

private:
    AsioBridgeStateV1 state_{};
    float linear_gain_{0.0F};
};

}  // namespace hibiki

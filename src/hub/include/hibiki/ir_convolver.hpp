#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_phase.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hibiki {

constexpr std::size_t kMaxRealtimeIrTapsV1 = 4096U;

struct IrConvolverStatusV1 {
    std::uint32_t schema_version{1};
    bool valid{false};
    std::uint32_t sample_rate{0U};
    std::uint32_t channels{0U};
    std::uint32_t kernel_channels{0U};
    std::size_t taps{0U};
    double declared_delay_ms{0.0};
    bool uses_fir{false};
};

// Fixed-capacity direct FIR convolver. The caller supplies a mono or
// per-channel kernel (interleaved channel-major: kernel[channel * taps + tap]);
// process_interleaved is in-place, allocation-free and bounded.
class IrConvolverV1 final {
public:
    [[nodiscard]] bool prepare(std::span<const float> kernel,
                               std::size_t taps,
                               std::uint32_t kernel_channels,
                               std::uint32_t channels,
                               std::uint32_t sample_rate,
                               const IrPhaseResolutionV1& phase) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process_interleaved(float* interleaved,
                                           std::size_t frames,
                                           std::uint32_t channels) noexcept;
    [[nodiscard]] const IrConvolverStatusV1& status() const noexcept { return status_; }
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return status_.sample_rate; }
    [[nodiscard]] std::uint32_t channels() const noexcept { return status_.channels; }

private:
    std::array<std::array<float, kMaxRealtimeIrTapsV1>, 8U> kernel_{};
    std::array<std::array<float, kMaxRealtimeIrTapsV1>, 8U> history_{};
    IrConvolverStatusV1 status_{};
    std::size_t write_index_{0U};
    bool prepared_{false};
};

}  // namespace hibiki

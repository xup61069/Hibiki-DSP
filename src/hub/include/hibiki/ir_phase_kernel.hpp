#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_phase.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hibiki {

// Control-plane result for a bounded phase transformation. The samples are
// channel-major (channel * taps + tap), so a caller can pass them directly to
// IrConvolverV1::prepare after validation. No RT path owns this object.
struct IrPhaseKernelResultV1 {
    std::uint32_t schema_version{1U};
    std::uint32_t sample_rate{0U};
    std::uint32_t kernel_channels{0U};
    std::size_t taps{0U};
    IrPhaseResolutionV1 resolution{};
    std::vector<float> channel_major{};
    bool valid{false};
    std::string diagnostic{};
};

// Build a finite, bounded phase-adjusted kernel from an existing real FIR.
//
// - MinimumPhase strength 0 keeps the source phase; strength 1 uses a real
//   cepstrum minimum-phase reconstruction.
// - MixedPhase interpolates source phase toward an integer-sample causal
//   linear-phase target while preserving the source magnitude.
// - LinearPhase uses that target directly according to strength.
// - Bypass returns the source unchanged so a graph can keep the IR detached;
//   callers must not attach the returned kernel for Strict Direct scenes.
//
// This deliberately runs only during control-plane prepare. It allocates and
// performs an FFT, and therefore must never be called from the audio callback.
[[nodiscard]] IrPhaseKernelResultV1 build_ir_phase_kernel_v1(
    std::span<const float> source_channel_major,
    std::size_t taps,
    std::uint32_t kernel_channels,
    std::uint32_t sample_rate,
    const IrPhaseResolutionV1& resolution) noexcept;

}  // namespace hibiki

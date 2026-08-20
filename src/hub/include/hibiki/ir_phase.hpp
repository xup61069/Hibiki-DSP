#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <cstdint>

namespace hibiki {

// This is a control-plane policy, not an IR coefficient format.  A caller
// supplies the IR and filter implementation; this contract only makes the
// latency/phase trade-off explicit and measurable before a graph commit.
enum class IrPhaseMode : std::uint8_t {
    MinimumPhase,
    MixedPhase,
    LinearPhase,
    Bypass,
};

struct IrPhasePolicyV1 {
    std::uint32_t schema_version{1};
    IrPhaseMode mode{IrPhaseMode::MinimumPhase};
    // 0 = no added buffering; 1 = strongest correction permitted by mode.
    double strength{0.0};
};

struct IrPhaseResolutionV1 {
    std::uint32_t schema_version{1};
    IrPhaseMode mode{IrPhaseMode::MinimumPhase};
    double strength{0.0};
    double added_delay_ms{0.0};
    bool uses_fir{false};
    bool valid{false};
};

inline constexpr double kIrPhaseBalancedMaxDelayMs = 80.0;
inline constexpr double kIrPhaseMovieMaxDelayMs = 160.0;

[[nodiscard]] bool validate_ir_phase_policy(const IrPhasePolicyV1& policy) noexcept;
[[nodiscard]] IrPhaseResolutionV1 resolve_ir_phase_policy(
    const IrPhasePolicyV1& policy) noexcept;

}  // namespace hibiki

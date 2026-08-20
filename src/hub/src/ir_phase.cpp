#include "hibiki/ir_phase.hpp"

#include <cmath>

namespace hibiki {

bool validate_ir_phase_policy(const IrPhasePolicyV1& policy) noexcept {
    if (policy.schema_version != 1 || !std::isfinite(policy.strength) ||
        policy.strength < 0.0 || policy.strength > 1.0) {
        return false;
    }
    switch (policy.mode) {
        case IrPhaseMode::MinimumPhase:
        case IrPhaseMode::MixedPhase:
        case IrPhaseMode::LinearPhase:
            return true;
        case IrPhaseMode::Bypass:
            return policy.strength == 0.0;
    }
    return false;
}

IrPhaseResolutionV1 resolve_ir_phase_policy(const IrPhasePolicyV1& policy) noexcept {
    IrPhaseResolutionV1 resolved{};
    resolved.mode = policy.mode;
    resolved.strength = policy.strength;
    if (!validate_ir_phase_policy(policy)) {
        return resolved;
    }

    resolved.valid = true;
    switch (policy.mode) {
        case IrPhaseMode::MinimumPhase:
        case IrPhaseMode::Bypass:
            // IIR/bypass paths do not add a buffering contract.  The actual
            // biquad group delay is measured separately from this policy.
            resolved.added_delay_ms = 0.0;
            resolved.uses_fir = false;
            break;
        case IrPhaseMode::MixedPhase:
            resolved.added_delay_ms = policy.strength * kIrPhaseBalancedMaxDelayMs;
            resolved.uses_fir = policy.strength > 0.0;
            break;
        case IrPhaseMode::LinearPhase:
            resolved.added_delay_ms = policy.strength * kIrPhaseMovieMaxDelayMs;
            resolved.uses_fir = policy.strength > 0.0;
            break;
    }
    return resolved;
}

}  // namespace hibiki

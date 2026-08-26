// SPDX-License-Identifier: GPL-3.0-only
#include "hibiki/ir_phase.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using hibiki::IrPhaseMode;
using hibiki::IrPhasePolicyV1;
using hibiki::IrPhaseResolutionV1;

}  // namespace

int main() {
    // validate: valid MinimumPhase policy with strength 0.
    {
        IrPhasePolicyV1 policy{};
        policy.schema_version = 1U;
        policy.mode = IrPhaseMode::MinimumPhase;
        policy.strength = 0.0;
        CHECK(hibiki::validate_ir_phase_policy(policy));
    }

    // validate: all three phase modes accept strength 1.0.
    {
        CHECK(hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::MinimumPhase, 1.0}));
        CHECK(hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, 1.0}));
        CHECK(hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 1.0}));
    }

    // validate: wrong schema version is rejected.
    CHECK(!hibiki::validate_ir_phase_policy(IrPhasePolicyV1{0U, IrPhaseMode::MinimumPhase, 0.5}));
    CHECK(!hibiki::validate_ir_phase_policy(IrPhasePolicyV1{2U, IrPhaseMode::MinimumPhase, 0.5}));

    // validate: negative strength is rejected.
    CHECK(!hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, -0.01}));

    // validate: strength > 1 is rejected.
    CHECK(!hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, 1.001}));

    // validate: non-finite (NaN) strength is rejected.
    CHECK(!hibiki::validate_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, std::numeric_limits<double>::quiet_NaN()}));

    // validate: non-finite (+inf) strength is rejected.
    CHECK(!hibiki::validate_ir_phase_policy(
        IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, std::numeric_limits<double>::infinity()}));

    // validate: Bypass requires strength == 0.
    CHECK(hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 0.0}));
    CHECK(!hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 0.1}));
    CHECK(!hibiki::validate_ir_phase_policy(IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 1.0}));

    // resolve: invalid policy returns valid=false and copies fields.
    {
        const auto resolved = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{2U, IrPhaseMode::MixedPhase, 0.5});
        CHECK(!resolved.valid);
        CHECK(resolved.schema_version == 1U);
        CHECK(resolved.mode == IrPhaseMode::MixedPhase);
    }

    // resolve: MinimumPhase adds no delay and does not use FIR.
    {
        const auto resolved = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::MinimumPhase, 1.0});
        CHECK(resolved.valid);
        CHECK(resolved.added_delay_ms == 0.0);
        CHECK(!resolved.uses_fir);
    }

    // resolve: Bypass adds no delay and does not use FIR.
    {
        const auto resolved = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::Bypass, 0.0});
        CHECK(resolved.valid);
        CHECK(resolved.added_delay_ms == 0.0);
        CHECK(!resolved.uses_fir);
    }

    // resolve: MixedPhase delay = strength * 80 ms, uses FIR when strength > 0.
    {
        const auto half = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, 0.5});
        CHECK(half.valid);
        CHECK(std::abs(half.added_delay_ms - 40.0) < 0.001);
        CHECK(half.uses_fir);

        const auto full = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, 1.0});
        CHECK(full.valid);
        CHECK(std::abs(full.added_delay_ms - 80.0) < 0.001);
        CHECK(full.uses_fir);

        const auto zero = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::MixedPhase, 0.0});
        CHECK(zero.valid);
        CHECK(zero.added_delay_ms == 0.0);
        CHECK(!zero.uses_fir);
    }

    // resolve: LinearPhase delay = strength * 160 ms, uses FIR when strength > 0.
    {
        const auto quarter = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 0.25});
        CHECK(quarter.valid);
        CHECK(std::abs(quarter.added_delay_ms - 40.0) < 0.001);
        CHECK(quarter.uses_fir);

        const auto full = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 1.0});
        CHECK(full.valid);
        CHECK(std::abs(full.added_delay_ms - 160.0) < 0.001);
        CHECK(full.uses_fir);

        const auto zero = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 0.0});
        CHECK(zero.valid);
        CHECK(zero.added_delay_ms == 0.0);
        CHECK(!zero.uses_fir);
    }

    // resolve: schema_version and mode are copied from input.
    {
        const auto resolved = hibiki::resolve_ir_phase_policy(
            IrPhasePolicyV1{1U, IrPhaseMode::LinearPhase, 0.5});
        CHECK(resolved.schema_version == 1U);
        CHECK(resolved.mode == IrPhaseMode::LinearPhase);
        CHECK(std::abs(resolved.strength - 0.5) < 0.0001);
    }

    std::fputs("All IR phase tests passed.\n", stdout);
    return 0;
}
